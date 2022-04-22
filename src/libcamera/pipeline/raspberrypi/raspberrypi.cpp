/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019-2021, Raspberry Pi (Trading) Ltd.
 *
 * raspberrypi.cpp - Pipeline handler for Raspberry Pi devices
 */
#include <algorithm>
#include <assert.h>
#include <cmath>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_set>

#include <libcamera/base/shared_fd.h>
#include <libcamera/base/utils.h>

#include <libcamera/camera.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/ipa/raspberrypi.h>
#include <libcamera/ipa/raspberrypi_ipa_interface.h>
#include <libcamera/ipa/raspberrypi_ipa_proxy.h>
#include <libcamera/logging.h>
#include <libcamera/property_ids.h>
#include <libcamera/request.h>

#include <linux/bcm2835-isp.h>
#include <linux/media-bus-format.h>
#include <linux/videodev2.h>

#include "libcamera/internal/bayer_format.h"
#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/ipa_manager.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/v4l2_videodevice.h"

#include "dma_heaps.h"
#include "rpi_stream.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(RPI)

namespace {

constexpr unsigned int defaultRawBitDepth = 12;

/* Map of mbus codes to supported sizes reported by the sensor. */
using SensorFormats = std::map<unsigned int, std::vector<Size>>;

SensorFormats populateSensorFormats(std::unique_ptr<CameraSensor> &sensor)
{
	SensorFormats formats;

	for (auto const mbusCode : sensor->mbusCodes())
		formats.emplace(mbusCode, sensor->sizes(mbusCode));

	return formats;
}

PixelFormat mbusCodeToPixelFormat(unsigned int mbus_code,
				  BayerFormat::Packing packingReq)
{
	BayerFormat bayer = BayerFormat::fromMbusCode(mbus_code);

	ASSERT(bayer.isValid());

	bayer.packing = packingReq;
	PixelFormat pix = bayer.toPixelFormat();

	/*
	 * Not all formats (e.g. 8-bit or 16-bit Bayer formats) can have packed
	 * variants. So if the PixelFormat returns as invalid, use the non-packed
	 * conversion instead.
	 */
	if (!pix.isValid()) {
		bayer.packing = BayerFormat::Packing::None;
		pix = bayer.toPixelFormat();
	}

	return pix;
}

V4L2DeviceFormat toV4L2DeviceFormat(const V4L2SubdeviceFormat &format,
				    BayerFormat::Packing packingReq)
{
	const PixelFormat pix = mbusCodeToPixelFormat(format.mbus_code, packingReq);
	V4L2DeviceFormat deviceFormat;

	deviceFormat.fourcc = V4L2PixelFormat::fromPixelFormat(pix);
	deviceFormat.size = format.size;
	deviceFormat.colorSpace = format.colorSpace;
	return deviceFormat;
}

bool isRaw(const PixelFormat &pixFmt)
{
	/*
	 * The isRaw test might be redundant right now the pipeline handler only
	 * supports RAW sensors. Leave it in for now, just as a sanity check.
	 */
	if (!pixFmt.isValid())
		return false;

	const PixelFormatInfo &info = PixelFormatInfo::info(pixFmt);
	if (!info.isValid())
		return false;

	return info.colourEncoding == PixelFormatInfo::ColourEncodingRAW;
}

double scoreFormat(double desired, double actual)
{
	double score = desired - actual;
	/* Smaller desired dimensions are preferred. */
	if (score < 0.0)
		score = (-score) / 8;
	/* Penalise non-exact matches. */
	if (actual != desired)
		score *= 2;

	return score;
}

V4L2SubdeviceFormat findBestFormat(const SensorFormats &formatsMap, const Size &req, unsigned int bitDepth)
{
	double bestScore = std::numeric_limits<double>::max(), score;
	V4L2SubdeviceFormat bestFormat;
	bestFormat.colorSpace = ColorSpace::Raw;

	constexpr float penaltyAr = 1500.0;
	constexpr float penaltyBitDepth = 500.0;

	/* Calculate the closest/best mode from the user requested size. */
	for (const auto &iter : formatsMap) {
		const unsigned int mbusCode = iter.first;
		const PixelFormat format = mbusCodeToPixelFormat(mbusCode,
								 BayerFormat::Packing::None);
		const PixelFormatInfo &info = PixelFormatInfo::info(format);

		for (const Size &size : iter.second) {
			double reqAr = static_cast<double>(req.width) / req.height;
			double fmtAr = static_cast<double>(size.width) / size.height;

			/* Score the dimensions for closeness. */
			score = scoreFormat(req.width, size.width);
			score += scoreFormat(req.height, size.height);
			score += penaltyAr * scoreFormat(reqAr, fmtAr);

			/* Add any penalties... this is not an exact science! */
			score += utils::abs_diff(info.bitsPerPixel, bitDepth) * penaltyBitDepth;

			if (score <= bestScore) {
				bestScore = score;
				bestFormat.mbus_code = mbusCode;
				bestFormat.size = size;
			}

			LOG(RPI, Debug) << "Format: " << size.toString()
					<< " fmt " << format.toString()
					<< " Score: " << score
					<< " (best " << bestScore << ")";
		}
	}

	return bestFormat;
}

enum class Unicam : unsigned int { Image, Embedded };
enum class Isp : unsigned int { Input, Output0, Output1, Stats };

} /* namespace */

class RPiCameraData : public Camera::Private
{
public:
	RPiCameraData(PipelineHandler *pipe)
		: Camera::Private(pipe), state_(State::Stopped),
		  supportsFlips_(false), flipsAlterBayerOrder_(false),
		  dropFrameCount_(0), ispOutputCount_(0)
	{
	}

	void frameStarted(uint32_t sequence);

	int loadIPA(ipa::RPi::SensorConfig *sensorConfig);
	int configureIPA(const CameraConfiguration *config);

	void statsMetadataComplete(uint32_t bufferId, const ControlList &controls);
	void runIsp(uint32_t bufferId);
	void embeddedComplete(uint32_t bufferId);
	void setIspControls(const ControlList &controls);
	void setDelayedControls(const ControlList &controls);
	void setSensorControls(ControlList &controls);

	/* bufferComplete signal handlers. */
	void unicamBufferDequeue(FrameBuffer *buffer);
	void ispInputDequeue(FrameBuffer *buffer);
	void ispOutputDequeue(FrameBuffer *buffer);

	void clearIncompleteRequests();
	void handleStreamBuffer(FrameBuffer *buffer, RPi::Stream *stream);
	void handleExternalBuffer(FrameBuffer *buffer, RPi::Stream *stream);
	void handleState();
	void applyScalerCrop(const ControlList &controls);

	std::unique_ptr<ipa::RPi::IPAProxyRPi> ipa_;

	std::unique_ptr<CameraSensor> sensor_;
	SensorFormats sensorFormats_;
	/* Array of Unicam and ISP device streams and associated buffers/streams. */
	RPi::Device<Unicam, 2> unicam_;
	RPi::Device<Isp, 4> isp_;
	/* The vector below is just for convenience when iterating over all streams. */
	std::vector<RPi::Stream *> streams_;
	/* Stores the ids of the buffers mapped in the IPA. */
	std::unordered_set<unsigned int> ipaBuffers_;

	/* DMAHEAP allocation helper. */
	RPi::DmaHeap dmaHeap_;
	SharedFD lsTable_;

	std::unique_ptr<DelayedControls> delayedCtrls_;
	bool sensorMetadata_;

	/*
	 * All the functions in this class are called from a single calling
	 * thread. So, we do not need to have any mutex to protect access to any
	 * of the variables below.
	 */
	enum class State { Stopped, Idle, Busy, IpaComplete };
	State state_;

	struct BayerFrame {
		FrameBuffer *buffer;
		ControlList controls;
	};

	std::queue<BayerFrame> bayerQueue_;
	std::queue<FrameBuffer *> embeddedQueue_;
	std::deque<Request *> requestQueue_;

	/*
	 * Manage horizontal and vertical flips supported (or not) by the
	 * sensor. Also store the "native" Bayer order (that is, with no
	 * transforms applied).
	 */
	bool supportsFlips_;
	bool flipsAlterBayerOrder_;
	BayerFormat::Order nativeBayerOrder_;

	/* For handling digital zoom. */
	IPACameraSensorInfo sensorInfo_;
	Rectangle ispCrop_; /* crop in ISP (camera mode) pixels */
	Rectangle scalerCrop_; /* crop in sensor native pixels */
	Size ispMinCropSize_;

	unsigned int dropFrameCount_;

private:
	void checkRequestCompleted();
	void fillRequestMetadata(const ControlList &bufferControls,
				 Request *request);
	void tryRunPipeline();
	bool findMatchingBuffers(BayerFrame &bayerFrame, FrameBuffer *&embeddedBuffer);

	unsigned int ispOutputCount_;
};

class RPiCameraConfiguration : public CameraConfiguration
{
public:
	RPiCameraConfiguration(const RPiCameraData *data);

	Status validate() override;

	/* Cache the combinedTransform_ that will be applied to the sensor */
	Transform combinedTransform_;

private:
	const RPiCameraData *data_;
};

class PipelineHandlerRPi : public PipelineHandler
{
public:
	PipelineHandlerRPi(CameraManager *manager);

	CameraConfiguration *generateConfiguration(Camera *camera, const StreamRoles &roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

private:
	RPiCameraData *cameraData(Camera *camera)
	{
		return static_cast<RPiCameraData *>(camera->_d());
	}

	int registerCamera(MediaDevice *unicam, MediaDevice *isp);
	int queueAllBuffers(Camera *camera);
	int prepareBuffers(Camera *camera);
	void freeBuffers(Camera *camera);
	void mapBuffers(Camera *camera, const RPi::BufferMap &buffers, unsigned int mask);
};

RPiCameraConfiguration::RPiCameraConfiguration(const RPiCameraData *data)
	: CameraConfiguration(), data_(data)
{
}

CameraConfiguration::Status RPiCameraConfiguration::validate()
{
	Status status = Valid;

	if (config_.empty())
		return Invalid;

	status = validateColorSpaces(ColorSpaceFlag::StreamsShareColorSpace);

	/*
	 * What if the platform has a non-90 degree rotation? We can't even
	 * "adjust" the configuration and carry on. Alternatively, raising an
	 * error means the platform can never run. Let's just print a warning
	 * and continue regardless; the rotation is effectively set to zero.
	 */
	int32_t rotation = data_->sensor_->properties().get(properties::Rotation);
	bool success;
	Transform rotationTransform = transformFromRotation(rotation, &success);
	if (!success)
		LOG(RPI, Warning) << "Invalid rotation of " << rotation
				  << " degrees - ignoring";
	Transform combined = transform * rotationTransform;

	/*
	 * We combine the platform and user transform, but must "adjust away"
	 * any combined result that includes a transform, as we can't do those.
	 * In this case, flipping only the transpose bit is helpful to
	 * applications - they either get the transform they requested, or have
	 * to do a simple transpose themselves (they don't have to worry about
	 * the other possible cases).
	 */
	if (!!(combined & Transform::Transpose)) {
		/*
		 * Flipping the transpose bit in "transform" flips it in the
		 * combined result too (as it's the last thing that happens),
		 * which is of course clearing it.
		 */
		transform ^= Transform::Transpose;
		combined &= ~Transform::Transpose;
		status = Adjusted;
	}

	/*
	 * We also check if the sensor doesn't do h/vflips at all, in which
	 * case we clear them, and the application will have to do everything.
	 */
	if (!data_->supportsFlips_ && !!combined) {
		/*
		 * If the sensor can do no transforms, then combined must be
		 * changed to the identity. The only user transform that gives
		 * rise to this the inverse of the rotation. (Recall that
		 * combined = transform * rotationTransform.)
		 */
		transform = -rotationTransform;
		combined = Transform::Identity;
		status = Adjusted;
	}

	/*
	 * Store the final combined transform that configure() will need to
	 * apply to the sensor to save us working it out again.
	 */
	combinedTransform_ = combined;

	unsigned int rawCount = 0, outCount = 0, count = 0, maxIndex = 0;
	std::pair<int, Size> outSize[2];
	Size maxSize;
	for (StreamConfiguration &cfg : config_) {
		if (isRaw(cfg.pixelFormat)) {
			/*
			 * Calculate the best sensor mode we can use based on
			 * the user request.
			 */
			const PixelFormatInfo &info = PixelFormatInfo::info(cfg.pixelFormat);
			unsigned int bitDepth = info.isValid() ? info.bitsPerPixel : defaultRawBitDepth;
			V4L2SubdeviceFormat sensorFormat = findBestFormat(data_->sensorFormats_, cfg.size, bitDepth);
			BayerFormat::Packing packing = BayerFormat::Packing::CSI2;
			if (info.isValid() && !info.packed)
				packing = BayerFormat::Packing::None;
			V4L2DeviceFormat unicamFormat = toV4L2DeviceFormat(sensorFormat,
									   packing);
			int ret = data_->unicam_[Unicam::Image].dev()->tryFormat(&unicamFormat);
			if (ret)
				return Invalid;

			/*
			 * Some sensors change their Bayer order when they are
			 * h-flipped or v-flipped, according to the transform.
			 * If this one does, we must advertise the transformed
			 * Bayer order in the raw stream. Note how we must
			 * fetch the "native" (i.e. untransformed) Bayer order,
			 * because the sensor may currently be flipped!
			 */
			V4L2PixelFormat fourcc = unicamFormat.fourcc;
			if (data_->flipsAlterBayerOrder_) {
				BayerFormat bayer = BayerFormat::fromV4L2PixelFormat(fourcc);
				bayer.order = data_->nativeBayerOrder_;
				bayer = bayer.transform(combined);
				fourcc = bayer.toV4L2PixelFormat();
			}

			PixelFormat unicamPixFormat = fourcc.toPixelFormat();
			if (cfg.size != unicamFormat.size ||
			    cfg.pixelFormat != unicamPixFormat) {
				cfg.size = unicamFormat.size;
				cfg.pixelFormat = unicamPixFormat;
				status = Adjusted;
			}

			cfg.stride = unicamFormat.planes[0].bpl;
			cfg.frameSize = unicamFormat.planes[0].size;
			rawCount++;
		} else {
			cfg.bufferCount = 3;
			outSize[outCount] = std::make_pair(count, cfg.size);
			/* Record the largest resolution for fixups later. */
			if (maxSize < cfg.size) {
				maxSize = cfg.size;
				maxIndex = outCount;
			}
			outCount++;
		}

		count++;

		/* Can only output 1 RAW stream, or 2 YUV/RGB streams. */
		if (rawCount > 1 || outCount > 2) {
			LOG(RPI, Error) << "Invalid number of streams requested";
			return Invalid;
		}
	}

	/*
	 * Now do any fixups needed. For the two ISP outputs, one stream must be
	 * equal or smaller than the other in all dimensions.
	 */
	for (unsigned int i = 0; i < outCount; i++) {
		outSize[i].second.width = std::min(outSize[i].second.width,
						   maxSize.width);
		outSize[i].second.height = std::min(outSize[i].second.height,
						    maxSize.height);

		if (config_.at(outSize[i].first).size != outSize[i].second) {
			config_.at(outSize[i].first).size = outSize[i].second;
			status = Adjusted;
		}

		/*
		 * Also validate the correct pixel formats here.
		 * Note that Output0 and Output1 support a different
		 * set of formats.
		 *
		 * Output 0 must be for the largest resolution. We will
		 * have that fixed up in the code above.
		 *
		 */
		StreamConfiguration &cfg = config_.at(outSize[i].first);
		PixelFormat &cfgPixFmt = cfg.pixelFormat;
		V4L2VideoDevice *dev;

		if (i == maxIndex)
			dev = data_->isp_[Isp::Output0].dev();
		else
			dev = data_->isp_[Isp::Output1].dev();

		V4L2VideoDevice::Formats fmts = dev->formats();

		if (fmts.find(V4L2PixelFormat::fromPixelFormat(cfgPixFmt)) == fmts.end()) {
			/* If we cannot find a native format, use a default one. */
			cfgPixFmt = formats::NV12;
			status = Adjusted;
		}

		V4L2DeviceFormat format;
		format.fourcc = V4L2PixelFormat::fromPixelFormat(cfg.pixelFormat);
		format.size = cfg.size;
		format.colorSpace = cfg.colorSpace;

		LOG(RPI, Debug)
			<< "Try color space " << ColorSpace::toString(cfg.colorSpace);

		int ret = dev->tryFormat(&format);
		if (ret)
			return Invalid;

		if (cfg.colorSpace != format.colorSpace) {
			status = Adjusted;
			LOG(RPI, Debug)
				<< "Color space changed from "
				<< ColorSpace::toString(cfg.colorSpace) << " to "
				<< ColorSpace::toString(format.colorSpace);
		}

		cfg.colorSpace = format.colorSpace;

		cfg.stride = format.planes[0].bpl;
		cfg.frameSize = format.planes[0].size;

	}

	return status;
}

PipelineHandlerRPi::PipelineHandlerRPi(CameraManager *manager)
	: PipelineHandler(manager)
{
}

CameraConfiguration *PipelineHandlerRPi::generateConfiguration(Camera *camera,
							       const StreamRoles &roles)
{
	RPiCameraData *data = cameraData(camera);
	CameraConfiguration *config = new RPiCameraConfiguration(data);
	V4L2SubdeviceFormat sensorFormat;
	unsigned int bufferCount;
	PixelFormat pixelFormat;
	V4L2VideoDevice::Formats fmts;
	Size size;
	std::optional<ColorSpace> colorSpace;

	if (roles.empty())
		return config;

	unsigned int rawCount = 0;
	unsigned int outCount = 0;
	Size sensorSize = data->sensor_->resolution();
	for (const StreamRole role : roles) {
		switch (role) {
		case StreamRole::Raw:
			size = sensorSize;
			sensorFormat = findBestFormat(data->sensorFormats_, size, defaultRawBitDepth);
			pixelFormat = mbusCodeToPixelFormat(sensorFormat.mbus_code,
							    BayerFormat::Packing::CSI2);
			ASSERT(pixelFormat.isValid());
			colorSpace = ColorSpace::Raw;
			bufferCount = 2;
			rawCount++;
			break;

		case StreamRole::StillCapture:
			fmts = data->isp_[Isp::Output0].dev()->formats();
			pixelFormat = formats::NV12;
			/*
			 * Still image codecs usually expect the JPEG color space.
			 * Even RGB codecs will be fine as the RGB we get with the
			 * JPEG color space is the same as sRGB.
			 */
			colorSpace = ColorSpace::Jpeg;
			/* Return the largest sensor resolution. */
			size = sensorSize;
			bufferCount = 1;
			outCount++;
			break;

		case StreamRole::VideoRecording:
			/*
			 * The colour denoise algorithm requires the analysis
			 * image, produced by the second ISP output, to be in
			 * YUV420 format. Select this format as the default, to
			 * maximize chances that it will be picked by
			 * applications and enable usage of the colour denoise
			 * algorithm.
			 */
			fmts = data->isp_[Isp::Output0].dev()->formats();
			pixelFormat = formats::YUV420;
			/*
			 * Choose a color space appropriate for video recording.
			 * Rec.709 will be a good default for HD resolutions.
			 */
			colorSpace = ColorSpace::Rec709;
			size = { 1920, 1080 };
			bufferCount = 4;
			outCount++;
			break;

		case StreamRole::Viewfinder:
			fmts = data->isp_[Isp::Output0].dev()->formats();
			pixelFormat = formats::ARGB8888;
			colorSpace = ColorSpace::Jpeg;
			size = { 800, 600 };
			bufferCount = 4;
			outCount++;
			break;

		default:
			LOG(RPI, Error) << "Requested stream role not supported: "
					<< role;
			delete config;
			return nullptr;
		}

		if (rawCount > 1 || outCount > 2) {
			LOG(RPI, Error) << "Invalid stream roles requested";
			delete config;
			return nullptr;
		}

		std::map<PixelFormat, std::vector<SizeRange>> deviceFormats;
		if (role == StreamRole::Raw) {
			/* Translate the MBUS codes to a PixelFormat. */
			for (const auto &format : data->sensorFormats_) {
				PixelFormat pf = mbusCodeToPixelFormat(format.first,
								       BayerFormat::Packing::CSI2);
				if (pf.isValid())
					deviceFormats.emplace(std::piecewise_construct,	std::forward_as_tuple(pf),
						std::forward_as_tuple(format.second.begin(), format.second.end()));
			}
		} else {
			/*
			 * Translate the V4L2PixelFormat to PixelFormat. Note that we
			 * limit the recommended largest ISP output size to match the
			 * sensor resolution.
			 */
			for (const auto &format : fmts) {
				PixelFormat pf = format.first.toPixelFormat();
				if (pf.isValid())
					deviceFormats[pf].emplace_back(sensorSize);
			}
		}

		/* Add the stream format based on the device node used for the use case. */
		StreamFormats formats(deviceFormats);
		StreamConfiguration cfg(formats);
		cfg.size = size;
		cfg.pixelFormat = pixelFormat;
		cfg.colorSpace = colorSpace;
		cfg.bufferCount = bufferCount;
		config->addConfiguration(cfg);
	}

	config->validate();

	return config;
}

int PipelineHandlerRPi::configure(Camera *camera, CameraConfiguration *config)
{
	RPiCameraData *data = cameraData(camera);
	int ret;

	/* Start by resetting the Unicam and ISP stream states. */
	for (auto const stream : data->streams_)
		stream->reset();

	BayerFormat::Packing packing = BayerFormat::Packing::CSI2;
	Size maxSize, sensorSize;
	unsigned int maxIndex = 0;
	bool rawStream = false;
	unsigned int bitDepth = defaultRawBitDepth;

	/*
	 * Look for the RAW stream (if given) size as well as the largest
	 * ISP output size.
	 */
	for (unsigned i = 0; i < config->size(); i++) {
		StreamConfiguration &cfg = config->at(i);

		if (isRaw(cfg.pixelFormat)) {
			/*
			 * If we have been given a RAW stream, use that size
			 * for setting up the sensor.
			 */
			sensorSize = cfg.size;
			rawStream = true;
			/* Check if the user has explicitly set an unpacked format. */
			BayerFormat bayerFormat = BayerFormat::fromPixelFormat(cfg.pixelFormat);
			packing = bayerFormat.packing;
			bitDepth = bayerFormat.bitDepth;
		} else {
			if (cfg.size > maxSize) {
				maxSize = config->at(i).size;
				maxIndex = i;
			}
		}
	}

	/*
	 * Configure the H/V flip controls based on the combination of
	 * the sensor and user transform.
	 */
	if (data->supportsFlips_) {
		const RPiCameraConfiguration *rpiConfig =
			static_cast<const RPiCameraConfiguration *>(config);
		ControlList controls;

		controls.set(V4L2_CID_HFLIP,
			     static_cast<int32_t>(!!(rpiConfig->combinedTransform_ & Transform::HFlip)));
		controls.set(V4L2_CID_VFLIP,
			     static_cast<int32_t>(!!(rpiConfig->combinedTransform_ & Transform::VFlip)));
		data->setSensorControls(controls);
	}

	/* First calculate the best sensor mode we can use based on the user request. */
	V4L2SubdeviceFormat sensorFormat = findBestFormat(data->sensorFormats_, rawStream ? sensorSize : maxSize, bitDepth);
	ret = data->sensor_->setFormat(&sensorFormat);
	if (ret)
		return ret;

	V4L2DeviceFormat unicamFormat = toV4L2DeviceFormat(sensorFormat, packing);
	ret = data->unicam_[Unicam::Image].dev()->setFormat(&unicamFormat);
	if (ret)
		return ret;

	LOG(RPI, Info) << "Sensor: " << camera->id()
		       << " - Selected sensor format: " << sensorFormat.toString()
		       << " - Selected unicam format: " << unicamFormat.toString();

	ret = data->isp_[Isp::Input].dev()->setFormat(&unicamFormat);
	if (ret)
		return ret;

	/*
	 * See which streams are requested, and route the user
	 * StreamConfiguration appropriately.
	 */
	V4L2DeviceFormat format;
	bool output0Set = false, output1Set = false;
	for (unsigned i = 0; i < config->size(); i++) {
		StreamConfiguration &cfg = config->at(i);

		if (isRaw(cfg.pixelFormat)) {
			cfg.setStream(&data->unicam_[Unicam::Image]);
			data->unicam_[Unicam::Image].setExternal(true);
			continue;
		}

		/* The largest resolution gets routed to the ISP Output 0 node. */
		RPi::Stream *stream = i == maxIndex ? &data->isp_[Isp::Output0]
						    : &data->isp_[Isp::Output1];

		V4L2PixelFormat fourcc = V4L2PixelFormat::fromPixelFormat(cfg.pixelFormat);
		format.size = cfg.size;
		format.fourcc = fourcc;
		format.colorSpace = cfg.colorSpace;

		LOG(RPI, Debug) << "Setting " << stream->name() << " to "
				<< format.toString();

		ret = stream->dev()->setFormat(&format);
		if (ret)
			return -EINVAL;

		if (format.size != cfg.size || format.fourcc != fourcc) {
			LOG(RPI, Error)
				<< "Failed to set requested format on " << stream->name()
				<< ", returned " << format.toString();
			return -EINVAL;
		}

		LOG(RPI, Debug)
			<< "Stream " << stream->name() << " has color space "
			<< ColorSpace::toString(cfg.colorSpace);

		cfg.setStream(stream);
		stream->setExternal(true);

		if (i != maxIndex)
			output1Set = true;
		else
			output0Set = true;
	}

	/*
	 * If ISP::Output0 stream has not been configured by the application,
	 * we must allow the hardware to generate an output so that the data
	 * flow in the pipeline handler remains consistent, and we still generate
	 * statistics for the IPA to use. So enable the output at a very low
	 * resolution for internal use.
	 *
	 * \todo Allow the pipeline to work correctly without Output0 and only
	 * statistics coming from the hardware.
	 */
	if (!output0Set) {
		maxSize = Size(320, 240);
		format = {};
		format.size = maxSize;
		format.fourcc = V4L2PixelFormat::fromPixelFormat(formats::YUV420);
		/* No one asked for output, so the color space doesn't matter. */
		format.colorSpace = ColorSpace::Jpeg;
		ret = data->isp_[Isp::Output0].dev()->setFormat(&format);
		if (ret) {
			LOG(RPI, Error)
				<< "Failed to set default format on ISP Output0: "
				<< ret;
			return -EINVAL;
		}

		LOG(RPI, Debug) << "Defaulting ISP Output0 format to "
				<< format.toString();
	}

	/*
	 * If ISP::Output1 stream has not been requested by the application, we
	 * set it up for internal use now. This second stream will be used for
	 * fast colour denoise, and must be a quarter resolution of the ISP::Output0
	 * stream. However, also limit the maximum size to 1200 pixels in the
	 * larger dimension, just to avoid being wasteful with buffer allocations
	 * and memory bandwidth.
	 *
	 * \todo If Output 1 format is not YUV420, Output 1 ought to be disabled as
	 * colour denoise will not run.
	 */
	if (!output1Set) {
		V4L2DeviceFormat output1Format = format;
		constexpr Size maxDimensions(1200, 1200);
		const Size limit = maxDimensions.boundedToAspectRatio(format.size);

		output1Format.size = (format.size / 2).boundedTo(limit).alignedDownTo(2, 2);

		LOG(RPI, Debug) << "Setting ISP Output1 (internal) to "
				<< output1Format.toString();

		ret = data->isp_[Isp::Output1].dev()->setFormat(&output1Format);
		if (ret) {
			LOG(RPI, Error) << "Failed to set format on ISP Output1: "
					<< ret;
			return -EINVAL;
		}
	}

	/* ISP statistics output format. */
	format = {};
	format.fourcc = V4L2PixelFormat(V4L2_META_FMT_BCM2835_ISP_STATS);
	ret = data->isp_[Isp::Stats].dev()->setFormat(&format);
	if (ret) {
		LOG(RPI, Error) << "Failed to set format on ISP stats stream: "
				<< format.toString();
		return ret;
	}

	/* Figure out the smallest selection the ISP will allow. */
	Rectangle testCrop(0, 0, 1, 1);
	data->isp_[Isp::Input].dev()->setSelection(V4L2_SEL_TGT_CROP, &testCrop);
	data->ispMinCropSize_ = testCrop.size();

	/* Adjust aspect ratio by providing crops on the input image. */
	Size size = unicamFormat.size.boundedToAspectRatio(maxSize);
	Rectangle crop = size.centeredTo(Rectangle(unicamFormat.size).center());
	data->ispCrop_ = crop;

	data->isp_[Isp::Input].dev()->setSelection(V4L2_SEL_TGT_CROP, &crop);

	ret = data->configureIPA(config);
	if (ret)
		LOG(RPI, Error) << "Failed to configure the IPA: " << ret;

	/*
	 * Configure the Unicam embedded data output format only if the sensor
	 * supports it.
	 */
	if (data->sensorMetadata_) {
		V4L2SubdeviceFormat embeddedFormat;

		data->sensor_->device()->getFormat(1, &embeddedFormat);
		format.fourcc = V4L2PixelFormat(V4L2_META_FMT_SENSOR_DATA);
		format.planes[0].size = embeddedFormat.size.width * embeddedFormat.size.height;

		LOG(RPI, Debug) << "Setting embedded data format.";
		ret = data->unicam_[Unicam::Embedded].dev()->setFormat(&format);
		if (ret) {
			LOG(RPI, Error) << "Failed to set format on Unicam embedded: "
					<< format.toString();
			return ret;
		}

		/*
		 * If a RAW/Bayer stream has been requested by the application,
		 * we must set both Unicam streams as external, even though the
		 * application may only request RAW frames. This is because we
		 * match timestamps on both streams to synchronise buffers.
		 */
		if (rawStream)
			data->unicam_[Unicam::Embedded].setExternal(true);
	}

	/*
	 * Update the ScalerCropMaximum to the correct value for this camera mode.
	 * For us, it's the same as the "analogue crop".
	 *
	 * \todo Make this property the ScalerCrop maximum value when dynamic
	 * controls are available and set it at validate() time
	 */
	data->properties_.set(properties::ScalerCropMaximum, data->sensorInfo_.analogCrop);

	return ret;
}

int PipelineHandlerRPi::exportFrameBuffers([[maybe_unused]] Camera *camera, Stream *stream,
					   std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	RPi::Stream *s = static_cast<RPi::Stream *>(stream);
	unsigned int count = stream->configuration().bufferCount;
	int ret = s->dev()->exportBuffers(count, buffers);

	s->setExportedBuffers(buffers);

	return ret;
}

int PipelineHandlerRPi::start(Camera *camera, const ControlList *controls)
{
	RPiCameraData *data = cameraData(camera);
	int ret;

	/* Allocate buffers for internal pipeline usage. */
	ret = prepareBuffers(camera);
	if (ret) {
		LOG(RPI, Error) << "Failed to allocate buffers";
		stop(camera);
		return ret;
	}

	/* Check if a ScalerCrop control was specified. */
	if (controls)
		data->applyScalerCrop(*controls);

	/* Start the IPA. */
	ipa::RPi::StartConfig startConfig;
	data->ipa_->start(controls ? *controls : ControlList{ controls::controls },
			  &startConfig);

	/* Apply any gain/exposure settings that the IPA may have passed back. */
	if (!startConfig.controls.empty())
		data->setSensorControls(startConfig.controls);

	/* Configure the number of dropped frames required on startup. */
	data->dropFrameCount_ = startConfig.dropFrameCount;

	/* We need to set the dropFrameCount_ before queueing buffers. */
	ret = queueAllBuffers(camera);
	if (ret) {
		LOG(RPI, Error) << "Failed to queue buffers";
		stop(camera);
		return ret;
	}

	/* Enable SOF event generation. */
	data->unicam_[Unicam::Image].dev()->setFrameStartEnabled(true);

	/*
	 * Reset the delayed controls with the gain and exposure values set by
	 * the IPA.
	 */
	data->delayedCtrls_->reset();

	data->state_ = RPiCameraData::State::Idle;

	/* Start all streams. */
	for (auto const stream : data->streams_) {
		ret = stream->dev()->streamOn();
		if (ret) {
			stop(camera);
			return ret;
		}
	}

	return 0;
}

void PipelineHandlerRPi::stopDevice(Camera *camera)
{
	RPiCameraData *data = cameraData(camera);

	data->state_ = RPiCameraData::State::Stopped;

	/* Disable SOF event generation. */
	data->unicam_[Unicam::Image].dev()->setFrameStartEnabled(false);

	for (auto const stream : data->streams_)
		stream->dev()->streamOff();

	data->clearIncompleteRequests();
	data->bayerQueue_ = {};
	data->embeddedQueue_ = {};

	/* Stop the IPA. */
	data->ipa_->stop();

	freeBuffers(camera);
}

int PipelineHandlerRPi::queueRequestDevice(Camera *camera, Request *request)
{
	RPiCameraData *data = cameraData(camera);

	if (data->state_ == RPiCameraData::State::Stopped)
		return -EINVAL;

	LOG(RPI, Debug) << "queueRequestDevice: New request.";

	/* Push all buffers supplied in the Request to the respective streams. */
	for (auto stream : data->streams_) {
		if (!stream->isExternal())
			continue;

		FrameBuffer *buffer = request->findBuffer(stream);
		if (buffer && stream->getBufferId(buffer) == -1) {
			/*
			 * This buffer is not recognised, so it must have been allocated
			 * outside the v4l2 device. Store it in the stream buffer list
			 * so we can track it.
			 */
			stream->setExternalBuffer(buffer);
		}
		/*
		 * If no buffer is provided by the request for this stream, we
		 * queue a nullptr to the stream to signify that it must use an
		 * internally allocated buffer for this capture request. This
		 * buffer will not be given back to the application, but is used
		 * to support the internal pipeline flow.
		 *
		 * The below queueBuffer() call will do nothing if there are not
		 * enough internal buffers allocated, but this will be handled by
		 * queuing the request for buffers in the RPiStream object.
		 */
		int ret = stream->queueBuffer(buffer);
		if (ret)
			return ret;
	}

	/* Push the request to the back of the queue. */
	data->requestQueue_.push_back(request);
	data->handleState();

	return 0;
}

bool PipelineHandlerRPi::match(DeviceEnumerator *enumerator)
{
	DeviceMatch unicam("unicam");
	MediaDevice *unicamDevice = acquireMediaDevice(enumerator, unicam);

	if (!unicamDevice) {
		LOG(RPI, Debug) << "Unable to acquire a Unicam instance";
		return false;
	}

	DeviceMatch isp("bcm2835-isp");
	MediaDevice *ispDevice = acquireMediaDevice(enumerator, isp);

	if (!ispDevice) {
		LOG(RPI, Debug) << "Unable to acquire ISP instance";
		return false;
	}

	int ret = registerCamera(unicamDevice, ispDevice);
	if (ret) {
		LOG(RPI, Error) << "Failed to register camera: " << ret;
		return false;
	}

	return true;
}

int PipelineHandlerRPi::registerCamera(MediaDevice *unicam, MediaDevice *isp)
{
	std::unique_ptr<RPiCameraData> data = std::make_unique<RPiCameraData>(this);

	if (!data->dmaHeap_.isValid())
		return -ENOMEM;

	MediaEntity *unicamImage = unicam->getEntityByName("unicam-image");
	MediaEntity *ispOutput0 = isp->getEntityByName("bcm2835-isp0-output0");
	MediaEntity *ispCapture1 = isp->getEntityByName("bcm2835-isp0-capture1");
	MediaEntity *ispCapture2 = isp->getEntityByName("bcm2835-isp0-capture2");
	MediaEntity *ispCapture3 = isp->getEntityByName("bcm2835-isp0-capture3");

	if (!unicamImage || !ispOutput0 || !ispCapture1 || !ispCapture2 || !ispCapture3)
		return -ENOENT;

	/* Locate and open the unicam video streams. */
	data->unicam_[Unicam::Image] = RPi::Stream("Unicam Image", unicamImage);

	/* An embedded data node will not be present if the sensor does not support it. */
	MediaEntity *unicamEmbedded = unicam->getEntityByName("unicam-embedded");
	if (unicamEmbedded) {
		data->unicam_[Unicam::Embedded] = RPi::Stream("Unicam Embedded", unicamEmbedded);
		data->unicam_[Unicam::Embedded].dev()->bufferReady.connect(data.get(),
									   &RPiCameraData::unicamBufferDequeue);
	}

	/* Tag the ISP input stream as an import stream. */
	data->isp_[Isp::Input] = RPi::Stream("ISP Input", ispOutput0, true);
	data->isp_[Isp::Output0] = RPi::Stream("ISP Output0", ispCapture1);
	data->isp_[Isp::Output1] = RPi::Stream("ISP Output1", ispCapture2);
	data->isp_[Isp::Stats] = RPi::Stream("ISP Stats", ispCapture3);

	/* Wire up all the buffer connections. */
	data->unicam_[Unicam::Image].dev()->frameStart.connect(data.get(), &RPiCameraData::frameStarted);
	data->unicam_[Unicam::Image].dev()->bufferReady.connect(data.get(), &RPiCameraData::unicamBufferDequeue);
	data->isp_[Isp::Input].dev()->bufferReady.connect(data.get(), &RPiCameraData::ispInputDequeue);
	data->isp_[Isp::Output0].dev()->bufferReady.connect(data.get(), &RPiCameraData::ispOutputDequeue);
	data->isp_[Isp::Output1].dev()->bufferReady.connect(data.get(), &RPiCameraData::ispOutputDequeue);
	data->isp_[Isp::Stats].dev()->bufferReady.connect(data.get(), &RPiCameraData::ispOutputDequeue);

	/* Identify the sensor. */
	for (MediaEntity *entity : unicam->entities()) {
		if (entity->function() == MEDIA_ENT_F_CAM_SENSOR) {
			data->sensor_ = std::make_unique<CameraSensor>(entity);
			break;
		}
	}

	if (!data->sensor_)
		return -EINVAL;

	if (data->sensor_->init())
		return -EINVAL;

	data->sensorFormats_ = populateSensorFormats(data->sensor_);

	ipa::RPi::SensorConfig sensorConfig;
	if (data->loadIPA(&sensorConfig)) {
		LOG(RPI, Error) << "Failed to load a suitable IPA library";
		return -EINVAL;
	}

	if (sensorConfig.sensorMetadata ^ !!unicamEmbedded) {
		LOG(RPI, Warning) << "Mismatch between Unicam and CamHelper for embedded data usage!";
		sensorConfig.sensorMetadata = false;
		if (unicamEmbedded)
			data->unicam_[Unicam::Embedded].dev()->bufferReady.disconnect();
	}

	/*
	 * Open all Unicam and ISP streams. The exception is the embedded data
	 * stream, which only gets opened below if the IPA reports that the sensor
	 * supports embedded data.
	 *
	 * The below grouping is just for convenience so that we can easily
	 * iterate over all streams in one go.
	 */
	data->streams_.push_back(&data->unicam_[Unicam::Image]);
	if (sensorConfig.sensorMetadata)
		data->streams_.push_back(&data->unicam_[Unicam::Embedded]);

	for (auto &stream : data->isp_)
		data->streams_.push_back(&stream);

	for (auto stream : data->streams_) {
		int ret = stream->dev()->open();
		if (ret)
			return ret;
	}

	if (!data->unicam_[Unicam::Image].dev()->caps().hasMediaController()) {
		LOG(RPI, Error) << "Unicam driver does not use the MediaController, please update your kernel!";
		return -EINVAL;
	}

	/*
	 * Setup our delayed control writer with the sensor default
	 * gain and exposure delays. Mark VBLANK for priority write.
	 */
	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN, { sensorConfig.gainDelay, false } },
		{ V4L2_CID_EXPOSURE, { sensorConfig.exposureDelay, false } },
		{ V4L2_CID_VBLANK, { sensorConfig.vblankDelay, true } }
	};
	data->delayedCtrls_ = std::make_unique<DelayedControls>(data->sensor_->device(), params);
	data->sensorMetadata_ = sensorConfig.sensorMetadata;

	/* Register the controls that the Raspberry Pi IPA can handle. */
	data->controlInfo_ = RPi::Controls;
	/* Initialize the camera properties. */
	data->properties_ = data->sensor_->properties();

	/*
	 * Set a default value for the ScalerCropMaximum property to show
	 * that we support its use, however, initialise it to zero because
	 * it's not meaningful until a camera mode has been chosen.
	 */
	data->properties_.set(properties::ScalerCropMaximum, Rectangle{});

	/*
	 * We cache three things about the sensor in relation to transforms
	 * (meaning horizontal and vertical flips).
	 *
	 * Firstly, does it support them?
	 * Secondly, if you use them does it affect the Bayer ordering?
	 * Thirdly, what is the "native" Bayer order, when no transforms are
	 * applied?
	 *
	 * As part of answering the final question, we reset the camera to
	 * no transform at all.
	 */
	const V4L2Subdevice *sensor = data->sensor_->device();
	const struct v4l2_query_ext_ctrl *hflipCtrl = sensor->controlInfo(V4L2_CID_HFLIP);
	if (hflipCtrl) {
		/* We assume it will support vflips too... */
		data->supportsFlips_ = true;
		data->flipsAlterBayerOrder_ = hflipCtrl->flags & V4L2_CTRL_FLAG_MODIFY_LAYOUT;

		ControlList ctrls(data->sensor_->controls());
		ctrls.set(V4L2_CID_HFLIP, 0);
		ctrls.set(V4L2_CID_VFLIP, 0);
		data->setSensorControls(ctrls);
	}

	/* Look for a valid Bayer format. */
	BayerFormat bayerFormat;
	for (const auto &iter : data->sensorFormats_) {
		bayerFormat = BayerFormat::fromMbusCode(iter.first);
		if (bayerFormat.isValid())
			break;
	}

	if (!bayerFormat.isValid()) {
		LOG(RPI, Error) << "No Bayer format found";
		return -EINVAL;
	}
	data->nativeBayerOrder_ = bayerFormat.order;

	/*
	 * List the available streams an application may request. At present, we
	 * do not advertise Unicam Embedded and ISP Statistics streams, as there
	 * is no mechanism for the application to request non-image buffer formats.
	 */
	std::set<Stream *> streams;
	streams.insert(&data->unicam_[Unicam::Image]);
	streams.insert(&data->isp_[Isp::Output0]);
	streams.insert(&data->isp_[Isp::Output1]);

	/* Create and register the camera. */
	const std::string &id = data->sensor_->id();
	std::shared_ptr<Camera> camera =
		Camera::create(std::move(data), id, streams);
	PipelineHandler::registerCamera(std::move(camera));

	LOG(RPI, Info) << "Registered camera " << id
		       << " to Unicam device " << unicam->deviceNode()
		       << " and ISP device " << isp->deviceNode();
	return 0;
}

int PipelineHandlerRPi::queueAllBuffers(Camera *camera)
{
	RPiCameraData *data = cameraData(camera);
	int ret;

	for (auto const stream : data->streams_) {
		if (!stream->isExternal()) {
			ret = stream->queueAllBuffers();
			if (ret < 0)
				return ret;
		} else {
			/*
			 * For external streams, we must queue up a set of internal
			 * buffers to handle the number of drop frames requested by
			 * the IPA. This is done by passing nullptr in queueBuffer().
			 *
			 * The below queueBuffer() call will do nothing if there
			 * are not enough internal buffers allocated, but this will
			 * be handled by queuing the request for buffers in the
			 * RPiStream object.
			 */
			unsigned int i;
			for (i = 0; i < data->dropFrameCount_; i++) {
				ret = stream->queueBuffer(nullptr);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

int PipelineHandlerRPi::prepareBuffers(Camera *camera)
{
	RPiCameraData *data = cameraData(camera);
	unsigned int numRawBuffers = 0;
	int ret;

	for (Stream *s : camera->streams()) {
		if (isRaw(s->configuration().pixelFormat)) {
			numRawBuffers = s->configuration().bufferCount;
			break;
		}
	}

	/* Decide how many internal buffers to allocate. */
	for (auto const stream : data->streams_) {
		unsigned int numBuffers;

		if (stream == &data->unicam_[Unicam::Image] ||
		    stream == &data->unicam_[Unicam::Embedded]) {
			/*
			 * For Unicam, allocate a minimum of 4 buffers as we want
			 * to avoid any frame drops. If an application has configured
			 * a RAW stream, allocate additional buffers to make up the
			 * minimum, but ensure we have at least 2 sets of internal
			 * buffers to use to minimise frame drops.
			 */
			constexpr unsigned int minBuffers = 4;
			numBuffers = std::max<int>(2, minBuffers - numRawBuffers);
		} else {
			/*
			 * Since the ISP runs synchronous with the IPA and requests,
			 * we only ever need one set of internal buffers. Any buffers
			 * the application wants to hold onto will already be exported
			 * through PipelineHandlerRPi::exportFrameBuffers().
			 */
			numBuffers = 1;
		}

		ret = stream->prepareBuffers(numBuffers);
		if (ret < 0)
			return ret;
	}

	/*
	 * Pass the stats and embedded data buffers to the IPA. No other
	 * buffers need to be passed.
	 */
	mapBuffers(camera, data->isp_[Isp::Stats].getBuffers(), ipa::RPi::MaskStats);
	if (data->sensorMetadata_)
		mapBuffers(camera, data->unicam_[Unicam::Embedded].getBuffers(),
			   ipa::RPi::MaskEmbeddedData);

	return 0;
}

void PipelineHandlerRPi::mapBuffers(Camera *camera, const RPi::BufferMap &buffers, unsigned int mask)
{
	RPiCameraData *data = cameraData(camera);
	std::vector<IPABuffer> ipaBuffers;
	/*
	 * Link the FrameBuffers with the id (key value) in the map stored in
	 * the RPi stream object - along with an identifier mask.
	 *
	 * This will allow us to identify buffers passed between the pipeline
	 * handler and the IPA.
	 */
	for (auto const &it : buffers) {
		ipaBuffers.push_back(IPABuffer(mask | it.first,
					       it.second->planes()));
		data->ipaBuffers_.insert(mask | it.first);
	}

	data->ipa_->mapBuffers(ipaBuffers);
}

void PipelineHandlerRPi::freeBuffers(Camera *camera)
{
	RPiCameraData *data = cameraData(camera);

	/* Copy the buffer ids from the unordered_set to a vector to pass to the IPA. */
	std::vector<unsigned int> ipaBuffers(data->ipaBuffers_.begin(), data->ipaBuffers_.end());
	data->ipa_->unmapBuffers(ipaBuffers);
	data->ipaBuffers_.clear();

	for (auto const stream : data->streams_)
		stream->releaseBuffers();
}

void RPiCameraData::frameStarted(uint32_t sequence)
{
	LOG(RPI, Debug) << "frame start " << sequence;

	/* Write any controls for the next frame as soon as we can. */
	delayedCtrls_->applyControls(sequence);
}

int RPiCameraData::loadIPA(ipa::RPi::SensorConfig *sensorConfig)
{
	ipa_ = IPAManager::createIPA<ipa::RPi::IPAProxyRPi>(pipe(), 1, 1);

	if (!ipa_)
		return -ENOENT;

	ipa_->statsMetadataComplete.connect(this, &RPiCameraData::statsMetadataComplete);
	ipa_->runIsp.connect(this, &RPiCameraData::runIsp);
	ipa_->embeddedComplete.connect(this, &RPiCameraData::embeddedComplete);
	ipa_->setIspControls.connect(this, &RPiCameraData::setIspControls);
	ipa_->setDelayedControls.connect(this, &RPiCameraData::setDelayedControls);

	/*
	 * The configuration (tuning file) is made from the sensor name unless
	 * the environment variable overrides it.
	 */
	std::string configurationFile;
	char const *configFromEnv = utils::secure_getenv("LIBCAMERA_RPI_TUNING_FILE");
	if (!configFromEnv || *configFromEnv == '\0')
		configurationFile = ipa_->configurationFile(sensor_->model() + ".json");
	else
		configurationFile = std::string(configFromEnv);

	IPASettings settings(configurationFile, sensor_->model());

	return ipa_->init(settings, sensorConfig);
}

int RPiCameraData::configureIPA(const CameraConfiguration *config)
{
	std::map<unsigned int, IPAStream> streamConfig;
	std::map<unsigned int, ControlInfoMap> entityControls;
	ipa::RPi::IPAConfig ipaConfig;

	/* Inform IPA of stream configuration and sensor controls. */
	unsigned int i = 0;
	for (auto const &stream : isp_) {
		if (stream.isExternal()) {
			streamConfig[i++] = IPAStream(
				stream.configuration().pixelFormat,
				stream.configuration().size);
		}
	}

	entityControls.emplace(0, sensor_->controls());
	entityControls.emplace(1, isp_[Isp::Input].dev()->controls());

	/* Always send the user transform to the IPA. */
	ipaConfig.transform = static_cast<unsigned int>(config->transform);

	/* Allocate the lens shading table via dmaHeap and pass to the IPA. */
	if (!lsTable_.isValid()) {
		lsTable_ = SharedFD(dmaHeap_.alloc("ls_grid", ipa::RPi::MaxLsGridSize));
		if (!lsTable_.isValid())
			return -ENOMEM;

		/* Allow the IPA to mmap the LS table via the file descriptor. */
		/*
		 * \todo Investigate if mapping the lens shading table buffer
		 * could be handled with mapBuffers().
		 */
		ipaConfig.lsTableHandle = lsTable_;
	}

	/* We store the IPACameraSensorInfo for digital zoom calculations. */
	int ret = sensor_->sensorInfo(&sensorInfo_);
	if (ret) {
		LOG(RPI, Error) << "Failed to retrieve camera sensor info";
		return ret;
	}

	/* Ready the IPA - it must know about the sensor resolution. */
	ControlList controls;
	ret = ipa_->configure(sensorInfo_, streamConfig, entityControls, ipaConfig,
			      &controls);
	if (ret < 0) {
		LOG(RPI, Error) << "IPA configuration failed!";
		return -EPIPE;
	}

	if (!controls.empty())
		setSensorControls(controls);

	return 0;
}

void RPiCameraData::statsMetadataComplete(uint32_t bufferId, const ControlList &controls)
{
	if (state_ == State::Stopped)
		return;

	FrameBuffer *buffer = isp_[Isp::Stats].getBuffers().at(bufferId);

	handleStreamBuffer(buffer, &isp_[Isp::Stats]);

	/* Add to the Request metadata buffer what the IPA has provided. */
	Request *request = requestQueue_.front();
	request->metadata().merge(controls);

	state_ = State::IpaComplete;
	handleState();
}

void RPiCameraData::runIsp(uint32_t bufferId)
{
	if (state_ == State::Stopped)
		return;

	FrameBuffer *buffer = unicam_[Unicam::Image].getBuffers().at(bufferId);

	LOG(RPI, Debug) << "Input re-queue to ISP, buffer id " << bufferId
			<< ", timestamp: " << buffer->metadata().timestamp;

	isp_[Isp::Input].queueBuffer(buffer);
	ispOutputCount_ = 0;
	handleState();
}

void RPiCameraData::embeddedComplete(uint32_t bufferId)
{
	if (state_ == State::Stopped)
		return;

	FrameBuffer *buffer = unicam_[Unicam::Embedded].getBuffers().at(bufferId);
	handleStreamBuffer(buffer, &unicam_[Unicam::Embedded]);
	handleState();
}

void RPiCameraData::setIspControls(const ControlList &controls)
{
	ControlList ctrls = controls;

	if (ctrls.contains(V4L2_CID_USER_BCM2835_ISP_LENS_SHADING)) {
		ControlValue &value =
			const_cast<ControlValue &>(ctrls.get(V4L2_CID_USER_BCM2835_ISP_LENS_SHADING));
		Span<uint8_t> s = value.data();
		bcm2835_isp_lens_shading *ls =
			reinterpret_cast<bcm2835_isp_lens_shading *>(s.data());
		ls->dmabuf = lsTable_.get();
	}

	isp_[Isp::Input].dev()->setControls(&ctrls);
	handleState();
}

void RPiCameraData::setDelayedControls(const ControlList &controls)
{
	if (!delayedCtrls_->push(controls))
		LOG(RPI, Error) << "V4L2 DelayedControl set failed";
	handleState();
}

void RPiCameraData::setSensorControls(ControlList &controls)
{
	/*
	 * We need to ensure that if both VBLANK and EXPOSURE are present, the
	 * former must be written ahead of, and separately from EXPOSURE to avoid
	 * V4L2 rejecting the latter. This is identical to what DelayedControls
	 * does with the priority write flag.
	 *
	 * As a consequence of the below logic, VBLANK gets set twice, and we
	 * rely on the v4l2 framework to not pass the second control set to the
	 * driver as the actual control value has not changed.
	 */
	if (controls.contains(V4L2_CID_EXPOSURE) && controls.contains(V4L2_CID_VBLANK)) {
		ControlList vblank_ctrl;

		vblank_ctrl.set(V4L2_CID_VBLANK, controls.get(V4L2_CID_VBLANK));
		sensor_->setControls(&vblank_ctrl);
	}

	sensor_->setControls(&controls);
}

void RPiCameraData::unicamBufferDequeue(FrameBuffer *buffer)
{
	RPi::Stream *stream = nullptr;
	int index;

	if (state_ == State::Stopped)
		return;

	for (RPi::Stream &s : unicam_) {
		index = s.getBufferId(buffer);
		if (index != -1) {
			stream = &s;
			break;
		}
	}

	/* The buffer must belong to one of our streams. */
	ASSERT(stream);

	LOG(RPI, Debug) << "Stream " << stream->name() << " buffer dequeue"
			<< ", buffer id " << index
			<< ", timestamp: " << buffer->metadata().timestamp;

	if (stream == &unicam_[Unicam::Image]) {
		/*
		 * Lookup the sensor controls used for this frame sequence from
		 * DelayedControl and queue them along with the frame buffer.
		 */
		ControlList ctrl = delayedCtrls_->get(buffer->metadata().sequence);
		/*
		 * Add the frame timestamp to the ControlList for the IPA to use
		 * as it does not receive the FrameBuffer object.
		 */
		ctrl.set(controls::SensorTimestamp, buffer->metadata().timestamp);
		bayerQueue_.push({ buffer, std::move(ctrl) });
	} else {
		embeddedQueue_.push(buffer);
	}

	handleState();
}

void RPiCameraData::ispInputDequeue(FrameBuffer *buffer)
{
	if (state_ == State::Stopped)
		return;

	LOG(RPI, Debug) << "Stream ISP Input buffer complete"
			<< ", buffer id " << unicam_[Unicam::Image].getBufferId(buffer)
			<< ", timestamp: " << buffer->metadata().timestamp;

	/* The ISP input buffer gets re-queued into Unicam. */
	handleStreamBuffer(buffer, &unicam_[Unicam::Image]);
	handleState();
}

void RPiCameraData::ispOutputDequeue(FrameBuffer *buffer)
{
	RPi::Stream *stream = nullptr;
	int index;

	if (state_ == State::Stopped)
		return;

	for (RPi::Stream &s : isp_) {
		index = s.getBufferId(buffer);
		if (index != -1) {
			stream = &s;
			break;
		}
	}

	/* The buffer must belong to one of our ISP output streams. */
	ASSERT(stream);

	LOG(RPI, Debug) << "Stream " << stream->name() << " buffer complete"
			<< ", buffer id " << index
			<< ", timestamp: " << buffer->metadata().timestamp;

	/*
	 * ISP statistics buffer must not be re-queued or sent back to the
	 * application until after the IPA signals so.
	 */
	if (stream == &isp_[Isp::Stats]) {
		ipa_->signalStatReady(ipa::RPi::MaskStats | static_cast<unsigned int>(index));
	} else {
		/* Any other ISP output can be handed back to the application now. */
		handleStreamBuffer(buffer, stream);
	}

	/*
	 * Increment the number of ISP outputs generated.
	 * This is needed to track dropped frames.
	 */
	ispOutputCount_++;

	handleState();
}

void RPiCameraData::clearIncompleteRequests()
{
	/*
	 * All outstanding requests (and associated buffers) must be returned
	 * back to the application.
	 */
	while (!requestQueue_.empty()) {
		Request *request = requestQueue_.front();

		for (auto &b : request->buffers()) {
			FrameBuffer *buffer = b.second;
			/*
			 * Has the buffer already been handed back to the
			 * request? If not, do so now.
			 */
			if (buffer->request()) {
				buffer->cancel();
				pipe()->completeBuffer(request, buffer);
			}
		}

		pipe()->completeRequest(request);
		requestQueue_.pop_front();
	}
}

void RPiCameraData::handleStreamBuffer(FrameBuffer *buffer, RPi::Stream *stream)
{
	if (stream->isExternal()) {
		/*
		 * It is possible to be here without a pending request, so check
		 * that we actually have one to action, otherwise we just return
		 * buffer back to the stream.
		 */
		Request *request = requestQueue_.empty() ? nullptr : requestQueue_.front();
		if (!dropFrameCount_ && request && request->findBuffer(stream) == buffer) {
			/*
			 * Check if this is an externally provided buffer, and if
			 * so, we must stop tracking it in the pipeline handler.
			 */
			handleExternalBuffer(buffer, stream);
			/*
			 * Tag the buffer as completed, returning it to the
			 * application.
			 */
			pipe()->completeBuffer(request, buffer);
		} else {
			/*
			 * This buffer was not part of the Request, or there is no
			 * pending request, so we can recycle it.
			 */
			stream->returnBuffer(buffer);
		}
	} else {
		/* Simply re-queue the buffer to the requested stream. */
		stream->queueBuffer(buffer);
	}
}

void RPiCameraData::handleExternalBuffer(FrameBuffer *buffer, RPi::Stream *stream)
{
	unsigned int id = stream->getBufferId(buffer);

	if (!(id & ipa::RPi::MaskExternalBuffer))
		return;

	/* Stop the Stream object from tracking the buffer. */
	stream->removeExternalBuffer(buffer);
}

void RPiCameraData::handleState()
{
	switch (state_) {
	case State::Stopped:
	case State::Busy:
		break;

	case State::IpaComplete:
		/* If the request is completed, we will switch to Idle state. */
		checkRequestCompleted();
		/*
		 * No break here, we want to try running the pipeline again.
		 * The fallthrough clause below suppresses compiler warnings.
		 */
		[[fallthrough]];

	case State::Idle:
		tryRunPipeline();
		break;
	}
}

void RPiCameraData::checkRequestCompleted()
{
	bool requestCompleted = false;
	/*
	 * If we are dropping this frame, do not touch the request, simply
	 * change the state to IDLE when ready.
	 */
	if (!dropFrameCount_) {
		Request *request = requestQueue_.front();
		if (request->hasPendingBuffers())
			return;

		/* Must wait for metadata to be filled in before completing. */
		if (state_ != State::IpaComplete)
			return;

		pipe()->completeRequest(request);
		requestQueue_.pop_front();
		requestCompleted = true;
	}

	/*
	 * Make sure we have three outputs completed in the case of a dropped
	 * frame.
	 */
	if (state_ == State::IpaComplete &&
	    ((ispOutputCount_ == 3 && dropFrameCount_) || requestCompleted)) {
		state_ = State::Idle;
		if (dropFrameCount_) {
			dropFrameCount_--;
			LOG(RPI, Debug) << "Dropping frame at the request of the IPA ("
					<< dropFrameCount_ << " left)";
		}
	}
}

void RPiCameraData::applyScalerCrop(const ControlList &controls)
{
	if (controls.contains(controls::ScalerCrop)) {
		Rectangle nativeCrop = controls.get<Rectangle>(controls::ScalerCrop);

		if (!nativeCrop.width || !nativeCrop.height)
			nativeCrop = { 0, 0, 1, 1 };

		/* Create a version of the crop scaled to ISP (camera mode) pixels. */
		Rectangle ispCrop = nativeCrop.translatedBy(-sensorInfo_.analogCrop.topLeft());
		ispCrop.scaleBy(sensorInfo_.outputSize, sensorInfo_.analogCrop.size());

		/*
		 * The crop that we set must be:
		 * 1. At least as big as ispMinCropSize_, once that's been
		 *    enlarged to the same aspect ratio.
		 * 2. With the same mid-point, if possible.
		 * 3. But it can't go outside the sensor area.
		 */
		Size minSize = ispMinCropSize_.expandedToAspectRatio(nativeCrop.size());
		Size size = ispCrop.size().expandedTo(minSize);
		ispCrop = size.centeredTo(ispCrop.center()).enclosedIn(Rectangle(sensorInfo_.outputSize));

		if (ispCrop != ispCrop_) {
			isp_[Isp::Input].dev()->setSelection(V4L2_SEL_TGT_CROP, &ispCrop);
			ispCrop_ = ispCrop;

			/*
			 * Also update the ScalerCrop in the metadata with what we actually
			 * used. But we must first rescale that from ISP (camera mode) pixels
			 * back into sensor native pixels.
			 */
			scalerCrop_ = ispCrop_.scaledBy(sensorInfo_.analogCrop.size(),
							sensorInfo_.outputSize);
			scalerCrop_.translateBy(sensorInfo_.analogCrop.topLeft());
		}
	}
}

void RPiCameraData::fillRequestMetadata(const ControlList &bufferControls,
					Request *request)
{
	request->metadata().set(controls::SensorTimestamp,
				bufferControls.get(controls::SensorTimestamp));

	request->metadata().set(controls::ScalerCrop, scalerCrop_);
}

void RPiCameraData::tryRunPipeline()
{
	FrameBuffer *embeddedBuffer;
	BayerFrame bayerFrame;

	/* If any of our request or buffer queues are empty, we cannot proceed. */
	if (state_ != State::Idle || requestQueue_.empty() ||
	    bayerQueue_.empty() || (embeddedQueue_.empty() && sensorMetadata_))
		return;

	if (!findMatchingBuffers(bayerFrame, embeddedBuffer))
		return;

	/* Take the first request from the queue and action the IPA. */
	Request *request = requestQueue_.front();

	/* See if a new ScalerCrop value needs to be applied. */
	applyScalerCrop(request->controls());

	/*
	 * Clear the request metadata and fill it with some initial non-IPA
	 * related controls. We clear it first because the request metadata
	 * may have been populated if we have dropped the previous frame.
	 */
	request->metadata().clear();
	fillRequestMetadata(bayerFrame.controls, request);

	/*
	 * Process all the user controls by the IPA. Once this is complete, we
	 * queue the ISP output buffer listed in the request to start the HW
	 * pipeline.
	 */
	ipa_->signalQueueRequest(request->controls());

	/* Set our state to say the pipeline is active. */
	state_ = State::Busy;

	unsigned int bayerId = unicam_[Unicam::Image].getBufferId(bayerFrame.buffer);

	LOG(RPI, Debug) << "Signalling signalIspPrepare:"
			<< " Bayer buffer id: " << bayerId;

	ipa::RPi::ISPConfig ispPrepare;
	ispPrepare.bayerBufferId = ipa::RPi::MaskBayerData | bayerId;
	ispPrepare.controls = std::move(bayerFrame.controls);

	if (embeddedBuffer) {
		unsigned int embeddedId = unicam_[Unicam::Embedded].getBufferId(embeddedBuffer);

		ispPrepare.embeddedBufferId = ipa::RPi::MaskEmbeddedData | embeddedId;
		ispPrepare.embeddedBufferPresent = true;

		LOG(RPI, Debug) << "Signalling signalIspPrepare:"
				<< " Bayer buffer id: " << embeddedId;
	}

	ipa_->signalIspPrepare(ispPrepare);
}

bool RPiCameraData::findMatchingBuffers(BayerFrame &bayerFrame, FrameBuffer *&embeddedBuffer)
{
	unsigned int embeddedRequeueCount = 0, bayerRequeueCount = 0;

	/* Loop until we find a matching bayer and embedded data buffer. */
	while (!bayerQueue_.empty()) {
		/* Start with the front of the bayer queue. */
		FrameBuffer *bayerBuffer = bayerQueue_.front().buffer;

		/*
		 * Find the embedded data buffer with a matching timestamp to pass to
		 * the IPA. Any embedded buffers with a timestamp lower than the
		 * current bayer buffer will be removed and re-queued to the driver.
		 */
		uint64_t ts = bayerBuffer->metadata().timestamp;
		embeddedBuffer = nullptr;
		while (!embeddedQueue_.empty()) {
			FrameBuffer *b = embeddedQueue_.front();
			if (!unicam_[Unicam::Embedded].isExternal() && b->metadata().timestamp < ts) {
				embeddedQueue_.pop();
				unicam_[Unicam::Embedded].queueBuffer(b);
				embeddedRequeueCount++;
				LOG(RPI, Warning) << "Dropping unmatched input frame in stream "
						  << unicam_[Unicam::Embedded].name();
			} else if (unicam_[Unicam::Embedded].isExternal() || b->metadata().timestamp == ts) {
				/* We pop the item from the queue lower down. */
				embeddedBuffer = b;
				break;
			} else {
				break; /* Only higher timestamps from here. */
			}
		}

		if (!embeddedBuffer) {
			bool flushedBuffers = false;

			LOG(RPI, Debug) << "Could not find matching embedded buffer";

			if (!sensorMetadata_) {
				/*
				 * If there is no sensor metadata, simply return the
				 * first bayer frame in the queue.
				 */
				LOG(RPI, Debug) << "Returning bayer frame without a match";
				bayerFrame = std::move(bayerQueue_.front());
				bayerQueue_.pop();
				embeddedBuffer = nullptr;
				return true;
			}

			if (!embeddedQueue_.empty()) {
				/*
				 * Not found a matching embedded buffer for the bayer buffer in
				 * the front of the queue. This buffer is now orphaned, so requeue
				 * it back to the device.
				 */
				unicam_[Unicam::Image].queueBuffer(bayerQueue_.front().buffer);
				bayerQueue_.pop();
				bayerRequeueCount++;
				LOG(RPI, Warning) << "Dropping unmatched input frame in stream "
						  << unicam_[Unicam::Image].name();
			}

			/*
			 * If we have requeued all available embedded data buffers in this loop,
			 * then we are fully out of sync, so might as well requeue all the pending
			 * bayer buffers.
			 */
			if (embeddedRequeueCount == unicam_[Unicam::Embedded].getBuffers().size()) {
				/* The embedded queue must be empty at this point! */
				ASSERT(embeddedQueue_.empty());

				LOG(RPI, Warning) << "Flushing bayer stream!";
				while (!bayerQueue_.empty()) {
					unicam_[Unicam::Image].queueBuffer(bayerQueue_.front().buffer);
					bayerQueue_.pop();
				}
				flushedBuffers = true;
			}

			/*
			 * Similar to the above, if we have requeued all available bayer buffers in
			 * the loop, then we are fully out of sync, so might as well requeue all the
			 * pending embedded data buffers.
			 */
			if (bayerRequeueCount == unicam_[Unicam::Image].getBuffers().size()) {
				/* The bayer queue must be empty at this point! */
				ASSERT(bayerQueue_.empty());

				LOG(RPI, Warning) << "Flushing embedded data stream!";
				while (!embeddedQueue_.empty()) {
					unicam_[Unicam::Embedded].queueBuffer(embeddedQueue_.front());
					embeddedQueue_.pop();
				}
				flushedBuffers = true;
			}

			/*
			 * If the embedded queue has become empty, we cannot do any more.
			 * Similarly, if we have flushed any one of our queues, we cannot do
			 * any more. Return from here without a buffer pair.
			 */
			if (embeddedQueue_.empty() || flushedBuffers)
				return false;
		} else {
			/*
			 * We have found a matching bayer and embedded data buffer, so
			 * nothing more to do apart from assigning the bayer frame and
			 * popping the buffers from the queue.
			 */
			bayerFrame = std::move(bayerQueue_.front());
			bayerQueue_.pop();
			embeddedQueue_.pop();
			return true;
		}
	}

	return false;
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerRPi)

} /* namespace libcamera */
