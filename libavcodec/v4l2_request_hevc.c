/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "hevcdec.h"
#include "hwconfig.h"
#include "v4l2_request.h"

#define MAX_SLICES 16

typedef struct V4L2RequestControlsHEVC {
    struct v4l2_ctrl_hevc_sps sps;
    struct v4l2_ctrl_hevc_pps pps;
    struct v4l2_ctrl_hevc_decode_params dec_params;
    struct v4l2_ctrl_hevc_scaling_matrix scaling_matrix;
    struct v4l2_ctrl_hevc_slice_params slice_params[MAX_SLICES];
    int first_slice;
    int num_slices; //TODO: this should be in control
} V4L2RequestControlsHEVC;

typedef struct V4L2RequestContextHEVC {
    V4L2RequestContext base;
    int decode_mode;
    int start_code;
    int max_slices;
} V4L2RequestContextHEVC;

static uint8_t nalu_slice_start_code[] = { 0x00, 0x00, 0x01 };

static void v4l2_request_hevc_fill_pred_table(const HEVCContext *h, struct v4l2_hevc_pred_weight_table *table)
{
    int32_t luma_weight_denom, chroma_weight_denom;
    const SliceHeader *sh = &h->sh;

    if (sh->slice_type == HEVC_SLICE_I ||
        (sh->slice_type == HEVC_SLICE_P && !h->ps.pps->weighted_pred_flag) ||
        (sh->slice_type == HEVC_SLICE_B && !h->ps.pps->weighted_bipred_flag))
        return;

    table->luma_log2_weight_denom = sh->luma_log2_weight_denom;

    if (h->ps.sps->chroma_format_idc)
        table->delta_chroma_log2_weight_denom = sh->chroma_log2_weight_denom - sh->luma_log2_weight_denom;

    luma_weight_denom = (1 << sh->luma_log2_weight_denom);
    chroma_weight_denom = (1 << sh->chroma_log2_weight_denom);

    for (int i = 0; i < 15 && i < sh->nb_refs[L0]; i++) {
        table->delta_luma_weight_l0[i] = sh->luma_weight_l0[i] - luma_weight_denom;
        table->luma_offset_l0[i] = sh->luma_offset_l0[i];
        table->delta_chroma_weight_l0[i][0] = sh->chroma_weight_l0[i][0] - chroma_weight_denom;
        table->delta_chroma_weight_l0[i][1] = sh->chroma_weight_l0[i][1] - chroma_weight_denom;
        table->chroma_offset_l0[i][0] = sh->chroma_offset_l0[i][0];
        table->chroma_offset_l0[i][1] = sh->chroma_offset_l0[i][1];
    }

    if (sh->slice_type != HEVC_SLICE_B)
        return;

    for (int i = 0; i < 15 && i < sh->nb_refs[L1]; i++) {
        table->delta_luma_weight_l1[i] = sh->luma_weight_l1[i] - luma_weight_denom;
        table->luma_offset_l1[i] = sh->luma_offset_l1[i];
        table->delta_chroma_weight_l1[i][0] = sh->chroma_weight_l1[i][0] - chroma_weight_denom;
        table->delta_chroma_weight_l1[i][1] = sh->chroma_weight_l1[i][1] - chroma_weight_denom;
        table->chroma_offset_l1[i][0] = sh->chroma_offset_l1[i][0];
        table->chroma_offset_l1[i][1] = sh->chroma_offset_l1[i][1];
    }
}

static uint8_t get_ref_pic_index(const HEVCContext *h, const HEVCFrame *frame,
                                 struct v4l2_ctrl_hevc_decode_params *dec_params)
{
    uint64_t timestamp;

    if (!frame)
        return 0;

    timestamp = ff_v4l2_request_get_capture_timestamp(frame->frame);

    for (uint8_t i = 0; i < dec_params->num_active_dpb_entries; i++) {
        struct v4l2_hevc_dpb_entry *entry = &dec_params->dpb[i];
        if (entry->timestamp == timestamp)
            return i;
    }

    return 0;
}

static void fill_dec_params(struct v4l2_ctrl_hevc_decode_params *dec_params, const HEVCContext *h)
{
    const HEVCFrame *pic = h->ref;
    const SliceHeader *sh = &h->sh;
    int i, entries = 0;

    *dec_params = (struct v4l2_ctrl_hevc_decode_params) {
        .pic_order_cnt_val = pic->poc, /* FIXME: is this same as slice_params->slice_pic_order_cnt ? */
	.num_poc_st_curr_before = h->rps[ST_CURR_BEF].nb_refs,
        .num_poc_st_curr_after = h->rps[ST_CURR_AFT].nb_refs,
        .num_poc_lt_curr = h->rps[LT_CURR].nb_refs,
    };

    for (i = 0; i < FF_ARRAY_ELEMS(h->DPB); i++) {
        const HEVCFrame *frame = &h->DPB[i];
        if (frame != pic && (frame->flags & (HEVC_FRAME_FLAG_LONG_REF | HEVC_FRAME_FLAG_SHORT_REF))) {
            struct v4l2_hevc_dpb_entry *entry = &dec_params->dpb[entries++];

            entry->timestamp = ff_v4l2_request_get_capture_timestamp(frame->frame);
            entry->field_pic = frame->frame->interlaced_frame;
            entry->flags = 0;
            if (frame->flags & HEVC_FRAME_FLAG_LONG_REF)
                entry->flags |= V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE;

            /* TODO: Interleaved: Get the POC for each field. */
            entry->pic_order_cnt_val = frame->poc;
        }
    }

    dec_params->num_active_dpb_entries = entries;

    if (IS_IRAP(h))
        dec_params->flags |= V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC;

    if (IS_IDR(h))
        dec_params->flags |= V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC;

    /* FIXME: is this really frame property? */
    if (sh->no_output_of_prior_pics_flag)
        dec_params->flags |= V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR;

    for (i = 0; i < V4L2_HEVC_DPB_ENTRIES_NUM_MAX; i++) {
        dec_params->poc_st_curr_before[i] = get_ref_pic_index(h, h->rps[ST_CURR_BEF].ref[i], dec_params);
        dec_params->poc_st_curr_after[i] = get_ref_pic_index(h, h->rps[ST_CURR_AFT].ref[i], dec_params);
        dec_params->poc_lt_curr[i] = get_ref_pic_index(h, h->rps[LT_CURR].ref[i], dec_params);
    }
}

static void v4l2_request_hevc_fill_slice_params(const HEVCContext *h,
                                                struct v4l2_ctrl_hevc_decode_params *dec_params,
                                                struct v4l2_ctrl_hevc_slice_params *slice_params)
{
    const HEVCFrame *pic = h->ref;
    const SliceHeader *sh = &h->sh;
    RefPicList *rpl;
    int i;

    *slice_params = (struct v4l2_ctrl_hevc_slice_params) {
        .bit_size = 0,
        .data_bit_offset = get_bits_count(&h->HEVClc->gb),

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
        .slice_segment_addr = sh->slice_segment_addr,

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: NAL unit header */
        .nal_unit_type = h->nal_unit_type,
        .nuh_temporal_id_plus1 = h->temporal_id + 1,

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
        .slice_type = sh->slice_type,
        .colour_plane_id = sh->colour_plane_id,
        .slice_pic_order_cnt = pic->poc,
        .num_ref_idx_l0_active_minus1 = sh->nb_refs[L0] ? sh->nb_refs[L0] - 1 : 0,
        .num_ref_idx_l1_active_minus1 = sh->nb_refs[L1] ? sh->nb_refs[L1] - 1 : 0,
        .collocated_ref_idx = sh->slice_temporal_mvp_enabled_flag ? sh->collocated_ref_idx : 0,
        .five_minus_max_num_merge_cand = sh->slice_type == HEVC_SLICE_I ? 0 : 5 - sh->max_num_merge_cand,
        .slice_qp_delta = sh->slice_qp_delta,
        .slice_cb_qp_offset = sh->slice_cb_qp_offset,
        .slice_cr_qp_offset = sh->slice_cr_qp_offset,
        .slice_act_y_qp_offset = 0,
        .slice_act_cb_qp_offset = 0,
        .slice_act_cr_qp_offset = 0,
        .slice_beta_offset_div2 = sh->beta_offset / 2,
        .slice_tc_offset_div2 = sh->tc_offset / 2,

        /* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture timing SEI message */
        .pic_struct = h->sei.picture_timing.picture_struct,
    };

    if (sh->slice_sample_adaptive_offset_flag[0])
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA;

    if (sh->slice_sample_adaptive_offset_flag[1])
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA;

    if (sh->slice_temporal_mvp_enabled_flag)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED;

    if (sh->mvd_l1_zero_flag)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO;

    if (sh->cabac_init_flag)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT;

    if (sh->collocated_list == L0)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0;

    if (sh->disable_deblocking_filter_flag)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED;

    if (sh->slice_loop_filter_across_slices_enabled_flag)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED;

    if (sh->dependent_slice_segment_flag)
        slice_params->flags |= V4L2_HEVC_SLICE_PARAMS_FLAG_DEPENDENT_SLICE_SEGMENT;

    if (sh->slice_type != HEVC_SLICE_I) {
        rpl = &h->ref->refPicList[0];
        for (i = 0; i < rpl->nb_refs; i++)
            slice_params->ref_idx_l0[i] = get_ref_pic_index(h, rpl->ref[i], dec_params);
    }

    if (sh->slice_type == HEVC_SLICE_B) {
        rpl = &h->ref->refPicList[1];
        for (i = 0; i < rpl->nb_refs; i++)
            slice_params->ref_idx_l1[i] = get_ref_pic_index(h, rpl->ref[i], dec_params);
    }

    v4l2_request_hevc_fill_pred_table(h, &slice_params->pred_weight_table);
}

static void fill_sps(struct v4l2_ctrl_hevc_sps *ctrl, const HEVCContext *h)
{
    const HEVCSPS *sps = h->ps.sps;

    /* ISO/IEC 23008-2, ITU-T Rec. H.265: Sequence parameter set */
    *ctrl = (struct v4l2_ctrl_hevc_sps) {
        .pic_width_in_luma_samples = sps->width,
        .pic_height_in_luma_samples = sps->height,
        .bit_depth_luma_minus8 = sps->bit_depth - 8,
        .bit_depth_chroma_minus8 = sps->bit_depth_chroma - 8,
        .log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_poc_lsb - 4,
        .sps_max_dec_pic_buffering_minus1 = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering - 1,
        .sps_max_num_reorder_pics = sps->temporal_layer[sps->max_sub_layers - 1].num_reorder_pics,
        .sps_max_latency_increase_plus1 = sps->temporal_layer[sps->max_sub_layers - 1].max_latency_increase + 1,
        .log2_min_luma_coding_block_size_minus3 = sps->log2_min_cb_size - 3,
        .log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_coding_block_size,
        .log2_min_luma_transform_block_size_minus2 = sps->log2_min_tb_size - 2,
        .log2_diff_max_min_luma_transform_block_size = sps->log2_max_trafo_size - sps->log2_min_tb_size,
        .max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter,
        .max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra,
        .pcm_sample_bit_depth_luma_minus1 = sps->pcm.bit_depth - 1,
        .pcm_sample_bit_depth_chroma_minus1 = sps->pcm.bit_depth_chroma - 1,
        .log2_min_pcm_luma_coding_block_size_minus3 = sps->pcm.log2_min_pcm_cb_size - 3,
        .log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size,
        .num_short_term_ref_pic_sets = sps->nb_st_rps,
        .num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps,
        .chroma_format_idc = sps->chroma_format_idc,
        .sps_max_sub_layers_minus1 = sps->max_sub_layers - 1,
    };

    if (sps->separate_colour_plane_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE;

    if (sps->scaling_list_enable_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED;

    if (sps->amp_enabled_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_AMP_ENABLED;

    if (sps->sao_enabled)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET;

    if (sps->pcm_enabled_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_PCM_ENABLED;

    if (sps->pcm.loop_filter_disable_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED;

    if (sps->long_term_ref_pics_present_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT;

    if (sps->sps_temporal_mvp_enabled_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED;

    if (sps->sps_strong_intra_smoothing_enable_flag)
        ctrl->flags |= V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED;
}

static int v4l2_request_hevc_start_frame(AVCodecContext *avctx,
                                         av_unused const uint8_t *buffer,
                                         av_unused uint32_t size)
{
    const HEVCContext *h = avctx->priv_data;
    const HEVCPPS *pps = h->ps.pps;
    const HEVCSPS *sps = h->ps.sps;
    const ScalingList *sl = pps->scaling_list_data_present_flag ?
                            &pps->scaling_list :
                            sps->scaling_list_enable_flag ?
                            &sps->scaling_list : NULL;
    V4L2RequestControlsHEVC *controls = h->ref->hwaccel_picture_private;

    fill_sps(&controls->sps, h);
    fill_dec_params(&controls->dec_params, h);

    if (sl) {
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 16; j++)
                controls->scaling_matrix.scaling_list_4x4[i][j] = sl->sl[0][i][j];
            for (int j = 0; j < 64; j++) {
                controls->scaling_matrix.scaling_list_8x8[i][j]   = sl->sl[1][i][j];
                controls->scaling_matrix.scaling_list_16x16[i][j] = sl->sl[2][i][j];
                if (i < 2)
                    controls->scaling_matrix.scaling_list_32x32[i][j] = sl->sl[3][i * 3][j];
            }
            controls->scaling_matrix.scaling_list_dc_coef_16x16[i] = sl->sl_dc[0][i];
            if (i < 2)
                controls->scaling_matrix.scaling_list_dc_coef_32x32[i] = sl->sl_dc[1][i * 3];
        }
    }

    /* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture parameter set */
    controls->pps = (struct v4l2_ctrl_hevc_pps) {
        .num_extra_slice_header_bits = pps->num_extra_slice_header_bits,
        .num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active - 1,
        .num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active - 1,
        .init_qp_minus26 = pps->pic_init_qp_minus26,
        .diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth,
        .pps_cb_qp_offset = pps->cb_qp_offset,
        .pps_cr_qp_offset = pps->cr_qp_offset,
        .pps_beta_offset_div2 = pps->beta_offset / 2,
        .pps_tc_offset_div2 = pps->tc_offset / 2,
        .log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level - 2,
    };

    if (pps->dependent_slice_segments_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED;

    if (pps->output_flag_present_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT;

    if (pps->sign_data_hiding_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED;

    if (pps->cabac_init_present_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT;

    if (pps->constrained_intra_pred_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED;

    if (pps->transform_skip_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED;

    if (pps->cu_qp_delta_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED;

    if (pps->pic_slice_level_chroma_qp_offsets_present_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT;

    if (pps->weighted_pred_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED;

    if (pps->weighted_bipred_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED;

    if (pps->transquant_bypass_enable_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED;

    if (pps->tiles_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_TILES_ENABLED;

    if (pps->entropy_coding_sync_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED;

    if (pps->loop_filter_across_tiles_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED;

    if (pps->seq_loop_filter_across_slices_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED;

    if (pps->deblocking_filter_override_enabled_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED;

    if (pps->disable_dbf)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER;

    if (pps->lists_modification_present_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT;

    if (pps->slice_header_extension_present_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT;

    if (pps->deblocking_filter_control_present_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT;

    if (pps->uniform_spacing_flag)
        controls->pps.flags |= V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING;

    if (pps->tiles_enabled_flag) {
        controls->pps.num_tile_columns_minus1 = pps->num_tile_columns - 1;
        controls->pps.num_tile_rows_minus1 = pps->num_tile_rows - 1;

        for (int i = 0; i < pps->num_tile_columns; i++)
            controls->pps.column_width_minus1[i] = pps->column_width[i] - 1;

        for (int i = 0; i < pps->num_tile_rows; i++)
            controls->pps.row_height_minus1[i] = pps->row_height[i] - 1;
    }

    controls->first_slice = 1;
    controls->num_slices = 0;

    return ff_v4l2_request_reset_frame(avctx, h->ref->frame);
}

static int v4l2_request_hevc_queue_decode(AVCodecContext *avctx, int last_slice)
{
    const HEVCContext *h = avctx->priv_data;
    V4L2RequestControlsHEVC *controls = h->ref->hwaccel_picture_private;
    V4L2RequestContextHEVC *ctx = avctx->internal->hwaccel_priv_data;

    struct v4l2_ext_control control[] = {
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SPS,
            .ptr = &controls->sps,
            .size = sizeof(controls->sps),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_PPS,
            .ptr = &controls->pps,
            .size = sizeof(controls->pps),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_DECODE_PARAMS,
            .ptr = &controls->dec_params,
            .size = sizeof(controls->dec_params),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SCALING_MATRIX,
            .ptr = &controls->scaling_matrix,
            .size = sizeof(controls->scaling_matrix),
        },
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS,
            .ptr = &controls->slice_params,
            .size = sizeof(controls->slice_params[0]) * FFMAX(FFMIN(controls->num_slices, MAX_SLICES), ctx->max_slices),
        },
    };

    if (ctx->decode_mode == V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED)
        return ff_v4l2_request_decode_slice(avctx, h->ref->frame, control, FF_ARRAY_ELEMS(control), controls->first_slice, last_slice);

    return ff_v4l2_request_decode_frame(avctx, h->ref->frame, control, FF_ARRAY_ELEMS(control));
}

static int v4l2_request_hevc_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const HEVCContext *h = avctx->priv_data;
    V4L2RequestControlsHEVC *controls = h->ref->hwaccel_picture_private;
    V4L2RequestContextHEVC *ctx = avctx->internal->hwaccel_priv_data;
    V4L2RequestDescriptor *req = (V4L2RequestDescriptor*)h->ref->frame->data[0];
    int ret, slice = FFMIN(controls->num_slices, MAX_SLICES - 1);

    if (ctx->decode_mode == V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED && slice) {
        ret = v4l2_request_hevc_queue_decode(avctx, 0);
        if (ret)
            return ret;

        ff_v4l2_request_reset_frame(avctx, h->ref->frame);
        slice = controls->num_slices = 0;
        controls->first_slice = 0;
    }

    v4l2_request_hevc_fill_slice_params(h, &controls->dec_params, &controls->slice_params[slice]);

    if (ctx->start_code == V4L2_MPEG_VIDEO_HEVC_START_CODE_ANNEX_B) {
        ret = ff_v4l2_request_append_output_buffer(avctx, h->ref->frame, nalu_slice_start_code, 3);
        if (ret)
            return ret;
    }

    ret = ff_v4l2_request_append_output_buffer(avctx, h->ref->frame, buffer, size);
    if (ret)
        return ret;

    controls->slice_params[slice].bit_size = req->output.used * 8; //FIXME
    controls->num_slices++;
    return 0;
}

static int v4l2_request_hevc_end_frame(AVCodecContext *avctx)
{
    return v4l2_request_hevc_queue_decode(avctx, 1);
}

static int v4l2_request_hevc_set_controls(AVCodecContext *avctx)
{
    V4L2RequestContextHEVC *ctx = avctx->internal->hwaccel_priv_data;
    int ret;

    struct v4l2_ext_control control[] = {
        { .id = V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE, },
        { .id = V4L2_CID_MPEG_VIDEO_HEVC_START_CODE, },
    };
    struct v4l2_query_ext_ctrl slice_params = {
        .id = V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS,
    };

    ctx->decode_mode = ff_v4l2_request_query_control_default_value(avctx, V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE);
    if (ctx->decode_mode != V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED &&
        ctx->decode_mode != V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_FRAME_BASED) {
        av_log(avctx, AV_LOG_ERROR, "%s: unsupported decode mode, %d\n", __func__, ctx->decode_mode);
        return AVERROR(EINVAL);
    }

    ctx->start_code = ff_v4l2_request_query_control_default_value(avctx, V4L2_CID_MPEG_VIDEO_HEVC_START_CODE);
    if (ctx->start_code != V4L2_MPEG_VIDEO_HEVC_START_CODE_NONE &&
        ctx->start_code != V4L2_MPEG_VIDEO_HEVC_START_CODE_ANNEX_B) {
        av_log(avctx, AV_LOG_ERROR, "%s: unsupported start code, %d\n", __func__, ctx->start_code);
        return AVERROR(EINVAL);
    }

    ret = ff_v4l2_request_query_control(avctx, &slice_params);
    if (ret)
        return ret;

    ctx->max_slices = slice_params.elems;
    if (ctx->max_slices > MAX_SLICES) {
        av_log(avctx, AV_LOG_ERROR, "%s: unsupported max slices, %d\n", __func__, ctx->max_slices);
        return AVERROR(EINVAL);
    }

    control[0].value = ctx->decode_mode;
    control[1].value = ctx->start_code;

    return ff_v4l2_request_set_controls(avctx, control, FF_ARRAY_ELEMS(control));
}

static int v4l2_request_hevc_init(AVCodecContext *avctx)
{
    const HEVCContext *h = avctx->priv_data;
    struct v4l2_ctrl_hevc_sps sps;
    int ret;

    struct v4l2_ext_control control[] = {
        {
            .id = V4L2_CID_MPEG_VIDEO_HEVC_SPS,
            .ptr = &sps,
            .size = sizeof(sps),
        },
    };

    fill_sps(&sps, h);

    ret = ff_v4l2_request_init(avctx, V4L2_PIX_FMT_HEVC_SLICE, 4 * 1024 * 1024, control, FF_ARRAY_ELEMS(control));
    if (ret)
        return ret;

    return v4l2_request_hevc_set_controls(avctx);
}

const AVHWAccel ff_hevc_v4l2request_hwaccel = {
    .name           = "hevc_v4l2request",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .pix_fmt        = AV_PIX_FMT_DRM_PRIME,
    .start_frame    = v4l2_request_hevc_start_frame,
    .decode_slice   = v4l2_request_hevc_decode_slice,
    .end_frame      = v4l2_request_hevc_end_frame,
    .frame_priv_data_size = sizeof(V4L2RequestControlsHEVC),
    .init           = v4l2_request_hevc_init,
    .uninit         = ff_v4l2_request_uninit,
    .priv_data_size = sizeof(V4L2RequestContextHEVC),
    .frame_params   = ff_v4l2_request_frame_params,
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
