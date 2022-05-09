// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011 Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 */

#include <common.h>
#include <env.h>
#include <image.h>
#include <image-android-dt.h>
#include <android_image.h>
#include <malloc.h>
#include <errno.h>
#include <asm/unaligned.h>
#include <mapmem.h>
#include <linux/libfdt.h>

#define ANDROID_IMAGE_DEFAULT_KERNEL_ADDR	0x10008000

static char andr_tmp_str[ANDR_BOOT_ARGS_SIZE + 1];

bool is_android_boot_image_header(const void *boot_img)
{
	return memcmp(ANDR_BOOT_MAGIC, boot_img, ANDR_BOOT_MAGIC_SIZE) == 0;
}

bool is_android_vendor_boot_image_header(const void *vendor_boot_img)
{
	return memcmp(VENDOR_BOOT_MAGIC, vendor_boot_img, ANDR_VENDOR_BOOT_MAGIC_SIZE) == 0;
}

void android_boot_image_v3_v4_get_data(
    const struct andr_boot_img_hdr_v3_v4 *hdr, struct andr_image_data *data) {
  ulong end;

  data->kcmdline = hdr->cmdline;

  /*
   * The header takes a full page, the remaining components are aligned
   * on page boundary.
   */
  end = (ulong)hdr;
  end += ANDR_GKI_PAGE_SIZE;
  data->kernel_ptr = end;
  data->kernel_size = hdr->kernel_size;
  end += ALIGN(hdr->kernel_size, ANDR_GKI_PAGE_SIZE);
  data->ramdisk_ptr = end;
  data->ramdisk_size = hdr->ramdisk_size;
  end += ALIGN(hdr->ramdisk_size, ANDR_GKI_PAGE_SIZE);

  if (hdr->header_version > 3) {
//    data->gki_signature_ptr = end;
//    data->gki_signature_size = hdr->signature_size;
    end += ALIGN(hdr->signature_size, ANDR_GKI_PAGE_SIZE);
  }

  data->boot_img_total_size = end - (ulong)hdr;
}

void android_boot_image_v0_v1_v2_get_data(
    const struct andr_boot_img_hdr_v0_v1_v2 *hdr,
    struct andr_image_data *data) {
  ulong end;

  data->image_name = hdr->name;
  data->kcmdline = hdr->cmdline;
  data->kcmdline_extra = hdr->extra_cmdline;
  /*
   * The header takes a full page, the remaining components are aligned
   * on page boundary
   */
  end = (ulong)hdr;
  end += hdr->page_size;
  data->kernel_ptr = end;
  data->kernel_size = hdr->kernel_size;
  end += ALIGN(hdr->kernel_size, hdr->page_size);
  data->ramdisk_ptr = end;
  data->ramdisk_size = hdr->ramdisk_size;
  end += ALIGN(hdr->ramdisk_size, hdr->page_size);
  data->second_ptr = end;
  data->second_size = hdr->second_size;
  end += ALIGN(hdr->second_size, hdr->page_size);

  if (hdr->header_version >= 1) {
    data->recovery_dtbo_ptr = end;
    data->recovery_dtbo_size = hdr->recovery_dtbo_size;
    end += ALIGN(hdr->recovery_dtbo_size, hdr->page_size);
  }

  if (hdr->header_version >= 2) {
    data->dtb_ptr = end;
    data->dtb_size = hdr->dtb_size;
    end += ALIGN(hdr->dtb_size, hdr->page_size);
  }

  data->boot_img_total_size = end - (ulong)hdr;
}

void android_vendor_boot_image_v3_v4_get_end(
    const struct andr_vendor_boot_img_hdr_v3_v4 *hdr,
    struct andr_image_data *data) {
  ulong end;

  /*
   * The header takes a full page, the remaining components are aligned
   * on page boundary.
   */
  data->kcmdline_extra = hdr->cmdline;
  data->tags_addr = hdr->tags_addr;
  data->image_name = hdr->name;

  end = (ulong)hdr;
  end += hdr->page_size;
  if (hdr->vendor_ramdisk_size) {
    // Override GKI ramdisk if vendor_ramdisk exists
    data->ramdisk_ptr = end;
    data->ramdisk_size = hdr->vendor_ramdisk_size;
    end += ALIGN(hdr->vendor_ramdisk_size, hdr->page_size);
  }

  data->dtb_ptr = end;
  data->dtb_size = hdr->dtb_size;

  end += ALIGN(hdr->dtb_size, hdr->page_size);
  end += ALIGN(hdr->vendor_ramdisk_table_size, hdr->page_size);
  end += ALIGN(hdr->bootconfig_size, hdr->page_size);
  data->boot_img_total_size = end - (ulong)hdr;
}

bool android_image_get_data(const void *boot_hdr, const void *vendor_boot_hdr,
                            struct andr_image_data *data) {
  if (!boot_hdr || !data) {
    printf("boot_hdr or data params can't be NULL\n");
    return false;
  }

  if (!is_android_boot_image_header(boot_hdr)) {
    printf("Incorrect boot image header\n");
    return false;
  }

  if (((struct andr_boot_img_hdr_v0_v1_v2 *)boot_hdr)->header_version > 2) {
    if (!vendor_boot_hdr) {
      printf("For boot header v3+ vendor_boot image has to be provided\n");
	  return false;
    }
    if (!is_android_vendor_boot_image_header(vendor_boot_hdr)) {
      printf("Incorrect vendor boot image header\n");
      return false;
    }
    android_boot_image_v3_v4_get_data(boot_hdr, data);
	android_vendor_boot_image_v3_v4_get_end(vendor_boot_hdr, data);
  } else {
    android_boot_image_v0_v1_v2_get_data(boot_hdr, data);
  }

  return true;
}

static ulong android_get_fixed_kernel_load_addr(struct andr_image_data *img_data)
{
	/*
	 * All the Android tools that generate a boot.img use this
	 * address as the default.
	 *
	 * Even though it doesn't really make a lot of sense, and it
	 * might be valid on some platforms, we treat that adress as
	 * the default value for this field, and try to execute the
	 * kernel in place in such a case.
	 *
	 * Otherwise, we will return the actual value set by the user.
	 */
	if (img_data->kernel_load_addr == ANDROID_IMAGE_DEFAULT_KERNEL_ADDR)
		return img_data->kernel_ptr;

	/*
	 * abootimg creates images where all load addresses are 0
	 * and we need to fix them.
	 */
	if (img_data->kernel_load_addr == 0 && img_data->ramdisk_load_addr == 0)
		return env_get_ulong("kernel_addr_r", 16, 0);

	return img_data->kernel_load_addr;
}

/**
 * android_image_get_kernel() - processes kernel part of Android boot images
 * @hdr:	Pointer to image header, which is at the start
 *			of the image.
 * @verify:	Checksum verification flag. Currently unimplemented.
 * @os_data:	Pointer to a ulong variable, will hold os data start
 *			address.
 * @os_len:	Pointer to a ulong variable, will hold os data length.
 *
 * This function returns the os image's start address and length. Also,
 * it appends the kernel command line to the bootargs env variable.
 *
 * Return: Zero, os start address and length on success,
 *		otherwise on failure.
 */
int android_image_get_kernel(const void *boot_img, const void *vendor_boot_img, int verify,
			     ulong *os_data, ulong *os_len)
{
	struct andr_image_data img_data = {0};
	if (!android_image_get_data(boot_img, vendor_boot_img, &img_data))
		return -1;

	/*
	 * Not all Android tools use the id field for signing the image with
	 * sha1 (or anything) so we don't check it. It is not obvious that the
	 * string is null terminated so we take care of this.
	 */
	strncpy(andr_tmp_str, img_data.image_name, ANDR_BOOT_NAME_SIZE);
	andr_tmp_str[ANDR_BOOT_NAME_SIZE] = '\0';
	if (strlen(andr_tmp_str))
		printf("Android's image name: %s\n", andr_tmp_str);

	u32 kernel_load_addr = android_get_fixed_kernel_load_addr(&img_data);
	printf("Kernel load addr 0x%08x size %u KiB\n",
	       kernel_load_addr, DIV_ROUND_UP(img_data.kernel_size, 1024));

	int len = 0;
	if (*img_data.kcmdline) {
		printf("Kernel command line: %s\n", img_data.kcmdline);
		len += strlen(img_data.kcmdline);
	}

	if (*img_data.kcmdline_extra) {
		printf("Kernel extra command line: %s\n", img_data.kcmdline_extra);
		len += strlen(img_data.kcmdline_extra);
	}

	char *bootargs = env_get("bootargs");
	if (bootargs)
		len += strlen(bootargs);

	char *newbootargs = malloc(len + 3);
	if (!newbootargs) {
		puts("Error: malloc in android_image_get_kernel failed!\n");
		return -ENOMEM;
	}
	*newbootargs = '\0';

	if (bootargs) {
		strcpy(newbootargs, bootargs);
		strcat(newbootargs, " ");
	}
	if (*img_data.kcmdline)
		strcat(newbootargs, img_data.kcmdline);

	if (*img_data.kcmdline_extra) {
		strcat(newbootargs, " ");
		strcat(newbootargs, img_data.kcmdline_extra);
	}

	env_set("bootargs", newbootargs);

	const struct image_header *ihdr = (const struct image_header *)img_data.kernel_ptr;

	if (os_data) {
		if (image_get_magic(ihdr) == IH_MAGIC) {
			*os_data = image_get_data(ihdr);
		} else {
			*os_data = img_data.kernel_ptr;
		}
	}
	if (os_len) {
		if (image_get_magic(ihdr) == IH_MAGIC)
			*os_len = image_get_data_size(ihdr);
		else
			*os_len = img_data.kernel_size;
	}
	return 0;
}

ulong android_image_get_kload(const void *boot_img, const void *vendor_boot_img)
{
	struct andr_image_data img_data = {0};
	if (!android_image_get_data(boot_img, vendor_boot_img, &img_data)) {
		return 0;
	}
	return android_get_fixed_kernel_load_addr(&img_data);
}

ulong android_image_get_kcomp(const void *boot_img, const void *vendor_boot_img)
{
	struct andr_image_data img_data = {0};
	if (!android_image_get_data(boot_img, vendor_boot_img, &img_data)) {
		return -EINVAL;
	}

	const void *p = (const void *)img_data.kernel_ptr;

	if (image_get_magic((image_header_t *)p) == IH_MAGIC)
		return image_get_comp((image_header_t *)p);
	else if (get_unaligned_le32(p) == LZ4F_MAGIC)
		return IH_COMP_LZ4;
	else
		return image_decomp_type(p, sizeof(u32));
}

int android_image_get_ramdisk(const void *boot_img, const void *vendor_boot_img,
			      ulong *rd_data, ulong *rd_len)
{
	struct andr_image_data img_data = {0};
	if (!android_image_get_data(boot_img, vendor_boot_img, &img_data)) {
		return -EINVAL;
	}

	if (!img_data.ramdisk_size) {
		*rd_data = *rd_len = 0;
		return -1;
	}

	printf("RAM disk load addr 0x%08lx size %u KiB\n",
	       img_data.ramdisk_ptr, DIV_ROUND_UP(img_data.ramdisk_size, 1024));

	*rd_data = img_data.ramdisk_ptr;
	*rd_len = img_data.ramdisk_size;
	return 0;
}

int android_image_get_second(void *boot_img, ulong *second_data, ulong *second_len)
{
	if (!is_android_boot_image_header(boot_img)) {
		printf("Invalid boot image");
		return -1;
	}

	struct andr_boot_img_hdr_v0_v1_v2 *hdr = boot_img;
	if (hdr->header_version > 2) {
		printf("Only supported for boot image version 2 and below");
		return -1;
	}

	if (!hdr->second_size) {
		*second_data = *second_len = 0;
		return -1;
	}

	*second_data = (unsigned long)hdr;
	*second_data += hdr->page_size;
	*second_data += ALIGN(hdr->kernel_size, hdr->page_size);
	*second_data += ALIGN(hdr->ramdisk_size, hdr->page_size);

	printf("second address is 0x%lx\n",*second_data);

	*second_len = hdr->second_size;
	return 0;
}

/**
 * android_image_get_dtbo() - Get address and size of recovery DTBO image.
 * @hdr_addr: Boot image header address
 * @addr: If not NULL, will contain address of recovery DTBO image
 * @size: If not NULL, will contain size of recovery DTBO image
 *
 * Get the address and size of DTBO image in "Recovery DTBO" area of Android
 * Boot Image in RAM. The format of this image is Android DTBO (see
 * corresponding "DTB/DTBO Partitions" AOSP documentation for details). Once
 * the address is obtained from this function, one can use 'adtimg' U-Boot
 * command or android_dt_*() functions to extract desired DTBO blob.
 *
 * This DTBO (included in boot image) is only needed for non-A/B devices, and it
 * only can be found in recovery image. On A/B devices we can always rely on
 * "dtbo" partition. See "Including DTBO in Recovery for Non-A/B Devices" in
 * AOSP documentation for details.
 *
 * Return: true on success or false on error.
 */
bool android_image_get_dtbo(const void *boot_img, ulong *addr, u32 *size)
{
	if (!is_android_boot_image_header(boot_img)) {
		printf("Invalid boot image");
		return false;
	}

	const struct andr_boot_img_hdr_v0_v1_v2 *hdr = boot_img;
	if (hdr->header_version < 1 || hdr->header_version > 2) {
		printf("Error: header_version must >= 1 and <=2 to get dtbo\n");
		return false;
	}

	if (hdr->recovery_dtbo_size == 0) {
		printf("Error: recovery_dtbo_size is 0\n");
		return false;
	}

	/* Calculate the address of DTB area in boot image */
	ulong dtbo_img_addr = (ulong)boot_img;
	dtbo_img_addr += hdr->page_size;
	dtbo_img_addr += ALIGN(hdr->kernel_size, hdr->page_size);
	dtbo_img_addr += ALIGN(hdr->ramdisk_size, hdr->page_size);
	dtbo_img_addr += ALIGN(hdr->second_size, hdr->page_size);

	if (addr)
		*addr = dtbo_img_addr;
	if (size)
		*size = hdr->recovery_dtbo_size;

	return true;
}

/**
 * android_image_get_dtb_by_index() - Get address and size of blob in DTB area.
 * @hdr_addr: Boot image header address
 * @index: Index of desired DTB in DTB area (starting from 0)
 * @addr: If not NULL, will contain address to specified DTB
 * @size: If not NULL, will contain size of specified DTB
 *
 * Get the address and size of DTB blob by its index in DTB area of Android
 * Boot Image in RAM.
 *
 * Return: true on success or false on error.
 */
bool android_image_get_dtb_by_index(const void *boot_img, const void *vendor_boot_img, u32 index, ulong *addr,
				    u32 *size)
{
	struct andr_image_data img_data = {0};
	if (!android_image_get_data(boot_img, vendor_boot_img, &img_data))
		return false;

	ulong dtb_img_addr;	/* address of DTB part in boot image */
	u32 dtb_img_size;	/* size of DTB payload in boot image */
	ulong dtb_addr;		/* address of DTB blob with specified index  */
	u32 i;			/* index iterator */

	dtb_img_addr = img_data.dtb_ptr;

	/* Check if DTB area of boot image is in DTBO format */
	if (android_dt_check_header(dtb_img_addr)) {
		return android_dt_get_fdt_by_index(dtb_img_addr, index, addr,
						   size);
	}

	/* Find out the address of DTB with specified index in concat blobs */
	dtb_img_size = img_data.dtb_size;
	i = 0;
	dtb_addr = dtb_img_addr;
	while (dtb_addr < dtb_img_addr + dtb_img_size) {
		const struct fdt_header *fdt;
		u32 dtb_size;

		fdt = map_sysmem(dtb_addr, sizeof(*fdt));
		if (fdt_check_header(fdt) != 0) {
			unmap_sysmem(fdt);
			printf("Error: Invalid FDT header for index %u\n", i);
			return false;
		}

		dtb_size = fdt_totalsize(fdt);
		unmap_sysmem(fdt);

		if (i == index) {
			if (size)
				*size = dtb_size;
			if (addr)
				*addr = dtb_addr;
			return true;
		}

		dtb_addr += dtb_size;
		++i;
	}

	printf("Error: Index is out of bounds (%u/%u)\n", index, i);
	return false;
}

#if !defined(CONFIG_SPL_BUILD)
/**
 * android_print_contents - prints out the contents of the Android boot image v3 or v4
 * @hdr: pointer to the Android boot image header v3 or v4
 *
 * android_print_contents() formats a multi line Android image contents
 * description.
 * The routine prints out Android image properties
 *
 * returns:
 *     no returned results
 */
static void
android_print_bootimg_v3_v4_contents(const struct andr_boot_img_hdr_v3_v4 *hdr)
{
	const char *const p = IMAGE_INDENT_STRING;
	/* os_version = ver << 11 | lvl */
	u32 os_ver = hdr->os_version >> 11;
	u32 os_lvl = hdr->os_version & ((1U << 11) - 1);

	printf("%skernel size:          %x\n", p, hdr->kernel_size);
	printf("%sramdisk size:         %x\n", p, hdr->ramdisk_size);

	printf("%sos_version:           %x (ver: %u.%u.%u, level: %u.%u)\n", p,
	       hdr->os_version, (os_ver >> 7) & 0x7F, (os_ver >> 14) & 0x7F,
	       os_ver & 0x7F, (os_lvl >> 4) + 2000, os_lvl & 0x0F);

	printf("%sheader size:          %x\n", p, hdr->header_size);
	printf("%sheader_version:       %d\n", p, hdr->header_version);
	printf("%scmdline:              %s\n", p, hdr->cmdline);

	if (hdr->header_version >= 4) {
		printf("%ssignature size:       %x\n", p, hdr->signature_size);
	}
}

/**
 * android_print_contents - prints out the contents of the Android format image
 * @hdr: pointer to the Android format image header
 *
 * android_print_contents() formats a multi line Android image contents
 * description.
 * The routine prints out Android image properties
 *
 * returns:
 *     no returned results
 */
void android_print_contents(const struct andr_boot_img_hdr_v0_v1_v2 *hdr)
{
	const char * const p = IMAGE_INDENT_STRING;

	if (hdr->header_version >= 3) {
		return android_print_bootimg_v3_v4_contents(
			(const struct andr_boot_img_hdr_v3_v4 *)hdr);
	}

	/* os_version = ver << 11 | lvl */
	u32 os_ver = hdr->os_version >> 11;
	u32 os_lvl = hdr->os_version & ((1U << 11) - 1);

	printf("%skernel size:          %x\n", p, hdr->kernel_size);
	printf("%skernel address:       %x\n", p, hdr->kernel_addr);
	printf("%sramdisk size:         %x\n", p, hdr->ramdisk_size);
	printf("%sramdisk address:      %x\n", p, hdr->ramdisk_addr);
	printf("%ssecond size:          %x\n", p, hdr->second_size);
	printf("%ssecond address:       %x\n", p, hdr->second_addr);
	printf("%stags address:         %x\n", p, hdr->tags_addr);
	printf("%spage size:            %x\n", p, hdr->page_size);
	/* ver = A << 14 | B << 7 | C         (7 bits for each of A, B, C)
	 * lvl = ((Y - 2000) & 127) << 4 | M  (7 bits for Y, 4 bits for M) */
	printf("%sos_version:           %x (ver: %u.%u.%u, level: %u.%u)\n",
	       p, hdr->os_version,
	       (os_ver >> 7) & 0x7F, (os_ver >> 14) & 0x7F, os_ver & 0x7F,
	       (os_lvl >> 4) + 2000, os_lvl & 0x0F);
	printf("%sname:                 %s\n", p, hdr->name);
	printf("%scmdline:              %s\n", p, hdr->cmdline);
	printf("%sheader_version:       %d\n", p, hdr->header_version);

	if (hdr->header_version >= 1) {
		printf("%srecovery dtbo size:   %x\n", p,
		       hdr->recovery_dtbo_size);
		printf("%srecovery dtbo offset: %llx\n", p,
		       hdr->recovery_dtbo_offset);
		printf("%sheader size:          %x\n", p,
		       hdr->header_size);
	}

	if (hdr->header_version == 2) {
		printf("%sdtb size:             %x\n", p, hdr->dtb_size);
		printf("%sdtb addr:             %llx\n", p, hdr->dtb_addr);
	}
}

/**
 * android_image_print_dtb_info - Print info for one DTB blob in DTB area.
 * @fdt: DTB header
 * @index: Number of DTB blob in DTB area.
 *
 * Return: true on success or false on error.
 */
static bool android_image_print_dtb_info(const struct fdt_header *fdt,
					 u32 index)
{
	int root_node_off;
	u32 fdt_size;
	const char *model;
	const char *compatible;

	root_node_off = fdt_path_offset(fdt, "/");
	if (root_node_off < 0) {
		printf("Error: Root node not found\n");
		return false;
	}

	fdt_size = fdt_totalsize(fdt);
	compatible = fdt_getprop(fdt, root_node_off, "compatible",
				 NULL);
	model = fdt_getprop(fdt, root_node_off, "model", NULL);

	printf(" - DTB #%u:\n", index);
	printf("           (DTB)size = %d\n", fdt_size);
	printf("          (DTB)model = %s\n", model ? model : "(unknown)");
	printf("     (DTB)compatible = %s\n",
	       compatible ? compatible : "(unknown)");

	return true;
}

/**
 * android_image_print_dtb_contents() - Print info for DTB blobs in DTB area.
 * @hdr_addr: Boot image header address
 *
 * DTB payload in Android Boot Image v2+ can be in one of following formats:
 *   1. Concatenated DTB blobs
 *   2. Android DTBO format (see CONFIG_CMD_ADTIMG for details)
 *
 * This function does next:
 *   1. Prints out the format used in DTB area
 *   2. Iterates over all DTB blobs in DTB area and prints out the info for
 *      each blob.
 *
 * Return: true on success or false on error.
 */
bool android_image_print_dtb_contents(const void *boot_img, const void *vendor_boot_img)
{
	struct andr_image_data img_data = {0};
	if (!android_image_get_data(boot_img, vendor_boot_img, &img_data))
		return false;

	bool res;
	u32 dtb_img_size = img_data.dtb_size;	/* size of DTB payload in boot image */
	ulong dtb_addr;		/* address of DTB blob with specified index  */
	u32 i;			/* index iterator */

	if (!dtb_img_size)
		return false;

	/* Check if DTB area of boot image is in DTBO format */
	if (android_dt_check_header(img_data.dtb_ptr)) {
		printf("## DTB area contents (DTBO format):\n");
		android_dt_print_contents(img_data.dtb_ptr);
		return true;
	}

	printf("## DTB area contents (concat format):\n");

	/* Iterate over concatenated DTB blobs */
	i = 0;
	dtb_addr = img_data.dtb_ptr;
	while (dtb_addr < img_data.dtb_ptr + dtb_img_size) {
		const struct fdt_header *fdt;
		u32 dtb_size;

		fdt = map_sysmem(dtb_addr, sizeof(*fdt));
		if (fdt_check_header(fdt) != 0) {
			unmap_sysmem(fdt);
			printf("Error: Invalid FDT header for index %u\n", i);
			return false;
		}

		res = android_image_print_dtb_info(fdt, i);
		if (!res) {
			unmap_sysmem(fdt);
			return false;
		}

		dtb_size = fdt_totalsize(fdt);
		unmap_sysmem(fdt);
		dtb_addr += dtb_size;
		++i;
	}

	return true;
}
#endif
