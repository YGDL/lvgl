/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    use_font_loader.c
  * @brief   这个函数主要实现LVGL的外部字库加载，对于小内存的设备无法加载整个字库文件
  *          到内存中，因此本文件提供了新的方法，只需要较小的内存即可使用外部的字库。
  *          同样，本文件也适用模拟器对外部文件进行仿真，只需要在lv_conf.h进行标准文
  *          件系统进行配置即可。
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2022 Sunshine Circuit.
  * All rights reserved.</center></h2>
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
 /* USER CODE END Header */
#include "use_font_loader.h"
#include "../lvgl.h"
#include "../misc/lv_fs.h"
#include "../misc/lv_gc.h"

#if LV_USE_FS_WIN32 != 1
#define bitmap_size 1024		/* glyph_bitmap缓冲区大小 */
use_fs_io use_file_io;			/* 全局文件数据 */
use_font_data use_font;			/* 全局字体数据 */		
#endif

#if LV_USE_FONT_COMPRESSED
static uint32_t use_rle_rdp;
static const uint8_t * use_rle_in;
static uint8_t use_rle_bpp;
static uint8_t use_rle_prev_v;
static uint8_t use_rle_cnt;
static use_rle_state_t use_rle_state;
#endif /*LV_USE_FONT_COMPRESSED*/

/* 函数声明 */
/************************************************** 加载函数使用 **************************************************/

lv_font_t * use_font_load(const char * font_name);
void use_font_free(lv_font_t * font);
bool use_font_get_glyph_dsc_fmt_txt(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next);
const uint8_t * use_font_get_bitmap_fmt_txt(const lv_font_t * font, uint32_t unicode_letter);

/* 静态函数声明 */
/************************************************** 加载函数使用 **************************************************/

static bool use_load_font(void * fp, lv_font_t * font);
static int32_t use_read_label(void * fp, uint32_t offset, const char * label);
static int32_t use_load_cmap(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t cmap_start);
static bool use_load_cmaps_tables(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t cmap_start, use_cmap_table * cmap_table);
static int32_t use_load_glyph(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t start, uint32_t * glyph_offset, uint32_t loca_count, use_font_header * header);
static int32_t use_load_kern(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint8_t format, uint32_t start);
static use_bit_iterator_t use_init_bit_iterator(void * fp);
static int32_t use_read_bits_signed(use_bit_iterator_t * it, int n_bits, lv_fs_res_t * res);
static uint32_t use_read_bits(use_bit_iterator_t * it, int n_bits, lv_fs_res_t * res);

/************************************************** 回调函数使用 **************************************************/
#if LV_USE_FS_WIN32 != 1
static bool use_get_glyph_dsc(const lv_font_t * font, uint32_t gid, lv_font_fmt_txt_glyph_dsc_t *gdsc);
#endif
static uint32_t use_get_glyph_dsc_id(const lv_font_t * font, uint32_t letter);
static int8_t use_get_kern_value(const lv_font_t * font, uint32_t gid_left, uint32_t gid_right);
static int32_t use_unicode_list_compare(const void * ref, const void * element);
static int32_t use_kern_pair_8_compare(const void * ref, const void * element);
static int32_t use_kern_pair_16_compare(const void * ref, const void * element);

#if LV_USE_FONT_COMPRESSED
static void use_decompress(const uint8_t * in, uint8_t * out, lv_coord_t w, lv_coord_t h, uint8_t bpp, bool prefilter);
static inline void use_decompress_line(uint8_t * out, lv_coord_t w);
static inline uint8_t use_get_bits(const uint8_t * in, uint32_t bit_pos, uint8_t len);
static inline void use_bits_write(uint8_t * out, uint32_t bit_pos, uint8_t val, uint8_t len);
static inline void use_rle_init(const uint8_t * in,  uint8_t bpp);
static inline uint8_t use_rle_next(void);
#endif /*LV_USE_FONT_COMPRESSED*/

/* 函数原型 */
/*********************************************** 字库加载函数 ***********************************************/
/**
 * @brief 自定义LVGL外部bin字库加载函数，部分加载，与LVGL官方函数lv_font_load不同
 * @note 
 * @param font_name 路径名，相对路径或绝对路径，相对路径需要在lv_conf.h钟修改
 * @return * lv_font_t* 
 * @retval font 指向lv_font_t结构的指针，不用需要释放
 */
lv_font_t * use_font_load(const char * font_name)
{
	/* 打开要加载的字体文件 */
#if LV_USE_FS_WIN32 != 1
	static FIL fp;		/* 如果使用static，通过指针能不能在其他地方用？ */
	use_font.font_path = font_name;
	/* 对于小内存使用FatFs的微型计算机，可以先使用FatFs加载文件后，直接利用文件的指针来进行文件的加载 */
	FRESULT res = f_open(&fp, (const TCHAR *)font_name, FA_READ);
#else	/* 在Windows上模拟时候选用win32组件 */
	lv_fs_file_t fp;
	lv_fs_res_t res = lv_fs_open(&fp, font_name, LV_FS_MODE_RD);
#endif
	if(LV_FS_RES_OK != res)
		return NULL;

	lv_font_t * font = lv_mem_alloc(sizeof(lv_font_t));
	//如果没申请到内存font = NULL
	if(font)
	{
		memset(font, 0, sizeof(lv_font_t));
		/* 传递的时候直接传递fp指针，在use_load_font中进行分别处理 */
		if(!use_load_font((void *)&fp, font))
		{
			LV_LOG_WARN("Error loading font file: %s\n", font_name);
			/*
			* 当'lvgl_load_font'失败时，它可以泄漏一些指针。
			* 所有非空指针都可以假定为已分配，'lv_font_free'应该正确释放它们。
			*/
			lv_font_free(font);
			font = NULL;
		}
	}
	//关闭加载的字体文件
#if LV_USE_FS_WIN32 != 1
	/* 当使用自定义的io函数时候无需释放，open之后会自动关闭文件 */
	f_close(&fp);
#else
	lv_fs_close(&fp);
#endif

	return font;
}

/**
 * @brief 外部字体的释放，在这里可以看到哪里申请了内存
 * @note  
 * @param font 待释放的内存指针 
 */
void use_font_free(lv_font_t * font)
{
	//font指针不为NULL
	if(NULL != font)
	{
		//描述为字体存储附加数据
		lv_font_fmt_txt_dsc_t * dsc = (lv_font_fmt_txt_dsc_t *)font->dsc;

		if(NULL != dsc)
		{
			//存储字距设置值。可以是'lv_font_fmt_txt_kern_pair_t *'或'lv_font_kern_classes_fmt_txt_t *'取决于'kern_classes'
			if(dsc->kern_classes == 0)
			{
				//来自对的字距值的简单映射。
				lv_font_fmt_txt_kern_pair_t * kern_dsc = (lv_font_fmt_txt_kern_pair_t *)dsc->kern_dsc;

				if(NULL != kern_dsc)
				{
					if(kern_dsc->glyph_ids)
						lv_mem_free((void *)kern_dsc->glyph_ids);

					if(kern_dsc->values)
						lv_mem_free((void *)kern_dsc->values);

					lv_mem_free((void *)kern_dsc);
				}
			}
			else
			{
				//更复杂但更优化的基于类的核值存储
				lv_font_fmt_txt_kern_classes_t * kern_dsc = (lv_font_fmt_txt_kern_classes_t *)dsc->kern_dsc;

				if(NULL != kern_dsc)
				{
					if(kern_dsc->class_pair_values)
						lv_mem_free((void *)kern_dsc->class_pair_values);

					if(kern_dsc->left_class_mapping)
						lv_mem_free((void *)kern_dsc->left_class_mapping);

					if(kern_dsc->right_class_mapping)
						lv_mem_free((void *)kern_dsc->right_class_mapping);

					lv_mem_free((void *)kern_dsc);
				}
			}

			//将代码点映射到'glyph_dsc'支持多种格式以优化内存使用
			//请参阅:https://github.com/lvgl/lv_font_conv/blob/master/doc/font_spec.md
			lv_font_fmt_txt_cmap_t * cmaps = (lv_font_fmt_txt_cmap_t *)dsc->cmaps;

			if(NULL != cmaps)
			{
				for(int i = 0; i < dsc->cmap_num; ++i)
				{
					if(NULL != cmaps[i].glyph_id_ofs_list)
						lv_mem_free((void *)cmaps[i].glyph_id_ofs_list);
					if(NULL != cmaps[i].unicode_list)
						lv_mem_free((void *)cmaps[i].unicode_list);
				}
				lv_mem_free(cmaps);
			}
			//所有字形的位图
			if(NULL != dsc->glyph_bitmap)
				lv_mem_free((void *)dsc->glyph_bitmap);
			//描述字形
			if(NULL != dsc->glyph_dsc)
				lv_mem_free((void *)dsc->glyph_dsc);

			lv_mem_free(dsc);
		}
		lv_mem_free(font);
	}
}

/**
 * @brief 外部字体文件读取
 * @note  从二进制文件加载'lv_font_t'，给定'lv_fs_file_t'。'lvgl_load_font'上的内存分配应立即归零，
 *        并且在任何可能的返回之前应在'lv_font_t'数据上设置指针。 当某些东西失败时，它会返回'false'，
 *        并且'lv_font_t'上的内存仍然需要使用'lv_font_free'释放。'lv_font_free'将假定所有非空指针都
 *        已分配并应释放。
 * @param fp 文件指针，对于没使用win32的文件系统，则为use_file_t类型
 * @param font 字体指针
 * @return true 
 * @return false 
 */
static bool use_load_font(void * fp, lv_font_t * font)
{
	lv_font_fmt_txt_dsc_t * font_dsc = (lv_font_fmt_txt_dsc_t *)lv_mem_alloc(sizeof(lv_font_fmt_txt_dsc_t));
	memset(font_dsc, 0, sizeof(lv_font_fmt_txt_dsc_t));
	font->dsc = font_dsc;

	/*header读取*/
	//文件0x00-0x0F数据结构
	//30 00 00 00 68 65 61 64 01 00 00 00 03 00 14 00
	//0  -  -  -  h  e  a  d  -  -  -  -  -  -  -  - 
	int32_t header_length = use_read_label(fp, 0, "head");
	if(header_length < 0)
		return false;

	//读取header数据
	//文件0x00-0x2F数据结构
	//30 00 00 00 68 65 61 64 01 00 00 00 03 00 14 00
	//12 00 FC FF 0F 00 FB FF 02 00 FC FF 12 00 00 00
	//10 00 01 01 00 04 06 05 06 01 00 00 01 00 00 00
	use_font_header font_header;	//字体信息句柄
	uint32_t read_length;
#if LV_USE_FS_WIN32 != 1
	/* 读取的时候可以不再使用FatFs的标准IO，能够实现更快速的读取 */
	if(FR_OK != f_read((FIL *)fp, &font_header, sizeof(use_font_header), &read_length) || sizeof(use_font_header) != read_length)
#else
	if(LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &font_header, sizeof(use_font_header), NULL))
#endif
		return false;
	
	font->base_line = -font_header.descent;
	font->line_height = font_header.ascent - font_header.descent;
	font->get_glyph_dsc = use_font_get_glyph_dsc_fmt_txt;
	font->get_glyph_bitmap = use_font_get_bitmap_fmt_txt;
	font->subpx = font_header.subpixels_mode;
	font->underline_position = font_header.underline_position;
	font->underline_thickness = font_header.underline_thickness;
	font->fallback = LV_FONT_DEFAULT;

	font_dsc->bpp = font_header.bits_per_pixel;
	font_dsc->kern_scale = font_header.kerning_scale;
	font_dsc->bitmap_format = font_header.compression_id;

	/*cmaps*/
	/* 代码映射到内部ID */
	uint32_t cmaps_start = header_length;
	int32_t cmaps_length = use_load_cmap(fp, font_dsc, cmaps_start);
	if(cmaps_length < 0)
		return false;

	/*loca*/
	/* 每个字形ID的数据偏移量 */
	uint32_t loca_start = cmaps_start + cmaps_length;
	int32_t loca_length = use_read_label(fp, loca_start, "loca");
	if(loca_length < 0)
		return false;

	/* 获取ID偏移的数据个数 */
	uint32_t loca_count;
#if LV_USE_FS_WIN32 != 1
	if(FR_OK != f_read((FIL *)fp, &loca_count, sizeof(uint32_t), &read_length) || sizeof(uint32_t) != read_length)
#else
	if(LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &loca_count, sizeof(uint32_t), NULL))
#endif
		return false;
	
	/* 获取loca中的各个ID offset，全部加载到glyph_offset里 */
#if LV_USE_FS_WIN32 == 1
	/* 数据量较大，在小内存的MCU中可以之际在Flash中查询，不需要加载进内存中 */
	bool failed = false;
	uint32_t * glyph_offset = lv_mem_alloc(sizeof(uint32_t) * (loca_count + 1));

	if(font_header.index_to_loc_format == 0)
	{
		for(unsigned int i = 0; i < loca_count; ++i)
		{
			uint16_t offset;
			if(LV_FS_RES_OK != lv_fs_read(fp, &offset, sizeof(uint16_t), NULL))
			{
				failed = true;
				break;
			}
			glyph_offset[i] = offset;
		}
	}
	else
	{
		if(font_header.index_to_loc_format == 1)
		{
			if(LV_FS_RES_OK != lv_fs_read(fp, glyph_offset, loca_count * sizeof(uint32_t), NULL))
				failed = true;
		}
		else
		{
			LV_LOG_WARN("Unknown index_to_loc_format: %d.", font_header.index_to_loc_format);
			failed = true;
		}
	}
	/* 内存申请 */
	if(failed)
	{
		lv_mem_free(glyph_offset);
		return false;
	}
#endif

	/*glyph*/
	/* 包含字形位图数据 */
	uint32_t glyph_start = loca_start + loca_length;

	/* 数据量较大，在小内存的MCU中可以之际在Flash中查询，不需要加载进内存中 */
#if LV_USE_FS_WIN32 != 1
	int32_t glyph_length = use_read_label(fp, glyph_start, "glyf");
	/* 申请内存用于存放字形bitmap */
	use_font.glyph_bitmap = (uint8_t *)lv_mem_alloc(sizeof(uint8_t) * bitmap_size);
#else	/* 使用win32模拟的时候，可以直接将字库数据加载进内存中 */
	int32_t glyph_length = use_load_glyph((lv_fs_file_t *)fp, font_dsc, glyph_start, glyph_offset, loca_count, &font_header);

	lv_mem_free(glyph_offset);
#endif

	if(glyph_length < 0)
		return false;

#if LV_USE_FS_WIN32 != 1
	/* 字库全局数据加载，后面的回调查询bitmap需要用到 */
	use_font.index_to_loc_format = font_header.index_to_loc_format;		/* 字库的偏移数据格式 */
	use_font.advance_width_bits = font_header.advance_width_bits;
	use_font.advance_width_format = font_header.advance_width_format;
	use_font.xy_bits = font_header.xy_bits;
	use_font.wh_bits = font_header.wh_bits;
	use_font.default_advance_width = font_header.default_advance_width;
	use_font.loca_start = loca_start + 12;								/* 偏移至ID偏移量段开始 */
	use_font.loca_length = loca_length;
	use_font.loca_count = loca_count;
	use_font.glyph_start = glyph_start;
	use_font.glyph_length = glyph_length;
	use_font.fp = (FIL *)fp;
#endif

	if(font_header.tables_count < 4)
	{
		font_dsc->kern_dsc = NULL;
		font_dsc->kern_classes = 0;
		font_dsc->kern_scale = 0;
		return true;
	}

	/* kerning */
	/* 字距信息 */
	uint32_t kern_start = glyph_start + glyph_length;

	int32_t kern_length = use_load_kern(fp, font_dsc, font_header.glyph_id_format, kern_start);

	return kern_length >= 0;
}

/**
 * @brief 读取数据标签，得到数据长度
 * @note  读取方式有两种，如果加载到内存中可以直接读取便宜，或者采用文件系统方式读取，自动区分采集的方式
 * @param fp 文件指针
 * @param offset 偏移地址
 * @param label 标签名指针，即字符串
 * @return int32_t 数据长度
 */
static int32_t use_read_label(void * fp, uint32_t offset, const char * label)
{
	uint32_t length;
#if LV_USE_FS_WIN32 != 1
	uint8_t buf[8];
	uint32_t read_length;

	/* 移动指针 */
	f_lseek(fp, offset);
	/* 读取flash中的前8字节数据 */
	if(FR_OK != f_read((FIL *)fp, buf, 8, &read_length) || 8 != read_length)
		return -1;

	/* 获取数据长度 */
	length = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

	/* 对比标签名，不符合的退出 */
	for(uint8_t i = 0U; i < 4; i ++)
	{
		if(buf[i + 4] != label[i])
			return -1;
	}
#else
	uint8_t buf[4];
	//设置流file_p的文件位置为给定的偏移pos，参数pos意味着从给定的whence位置查找的字节数。
	lv_fs_seek(fp, offset, LV_FS_SEEK_SET);
	//读取了字节后文件指针会自动偏移
	if(LV_FS_RES_OK != lv_fs_read(fp, &length, 4, NULL) || LV_FS_RES_OK != lv_fs_read(fp, buf, 4, NULL) || 0 != memcmp(label, buf, 4))
	{
		LV_LOG_WARN("Error reading '%s' label.", label);
		return -1;
	}
#endif

	return length;
}

/**
 * @brief 如果字体未压缩，则用作LittelvGL本机字体格式中的'get_glyph_dsc'回调。
 * @note 回调函数
 * @param font 指向字体的指针
 * @param dsc_out 在此处存储结果描述符
 * @param unicode_letter 一个UNICODE字母代码
 * @param unicode_letter_next 下一个UNICODE代码
 * @return true 描述符已成功加载到'dsc_out'中。
 * @return false 未找到字母，没有数据加载到'dsc_out'
 */
bool use_font_get_glyph_dsc_fmt_txt(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next)
{
	bool is_tab = false;
	if(unicode_letter == '\t')
	{
		unicode_letter = ' ';
		is_tab = true;
	}

	lv_font_fmt_txt_dsc_t * fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;
	uint32_t gid = use_get_glyph_dsc_id(font, unicode_letter);
	if(!gid)
		return false;

	int8_t kvalue = 0;
	if(fdsc->kern_dsc)
	{
		uint32_t gid_next = use_get_glyph_dsc_id(font, unicode_letter_next);
		if(gid_next)
			kvalue = use_get_kern_value(font, gid, gid_next);
	}

	/* Put together a glyph dsc */
	/* 在这里找到bitmap的偏移，即loca的数据，并将指针传递给gdsc，注意不是解引用 */
#if LV_USE_FS_WIN32 != 1
	/* 创建auto变量，callback完成自动释放，若使用malloc需要手动释放 */
	lv_font_fmt_txt_glyph_dsc_t _gdsc;
	/* 指针传递 */
	lv_font_fmt_txt_glyph_dsc_t * gdsc = &_gdsc;
	if(true != use_get_glyph_dsc(font, gid, gdsc))
		return false;
#else
	/* 指针转换，数据已经加载进内存中了，不需要重新申请内存 */
	const lv_font_fmt_txt_glyph_dsc_t * gdsc = &fdsc->glyph_dsc[gid];
#endif

	int32_t kv = ((int32_t)((int32_t)kvalue * fdsc->kern_scale) >> 4);

	uint32_t adv_w = gdsc->adv_w;
	if(is_tab)
		adv_w *= 2;

	adv_w += kv;
	adv_w  = (adv_w + (1 << 3)) >> 4;

	dsc_out->adv_w = adv_w;
	dsc_out->box_h = gdsc->box_h;
	dsc_out->box_w = gdsc->box_w;
	dsc_out->ofs_x = gdsc->ofs_x;
	dsc_out->ofs_y = gdsc->ofs_y;
	dsc_out->bpp   = (uint8_t)fdsc->bpp;
	dsc_out->is_placeholder = false;

	if(is_tab)
		dsc_out->box_w = dsc_out->box_w * 2;

#if LV_USE_FS_WIN32 != 1
	use_font.box_w = dsc_out->box_w;
	use_font.box_h = dsc_out->box_h;
	use_font.ofs_x = dsc_out->ofs_x;
	use_font.ofs_y = dsc_out->ofs_y;
#endif

	return true;
}

/**
 * @brief 如果字体未压缩，则用作LittelVGL原生字体格式中的'get_glyph_bitmap'回调。
 * @note 回调函数
 * @param font 字体指针
 * @param unicode_letter 应该获取位图的Unicode代码
 * @return const uint8_t* 指向位图或NULL的指针（如果未找到）
 */
const uint8_t * use_font_get_bitmap_fmt_txt(const lv_font_t * font, uint32_t unicode_letter)
{
	if(unicode_letter == '\t')
		unicode_letter = ' ';

	lv_font_fmt_txt_dsc_t * fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;
	/* gid号， */
	uint32_t gid = use_get_glyph_dsc_id(font, unicode_letter);
	if(!gid)
		return NULL;

	/* WIN32与FATFS条件编译 */
#if LV_USE_FS_WIN32 != 1
	/* 获取全局字库数据 */
	FIL * fp = use_font.fp;
	uint32_t now;		/* 当前gid号 */
	uint32_t next;		/* 顺序下一个gid号 */
	uint32_t read_length;
	lv_fs_res_t res = LV_FS_RES_OK;

	f_open(fp, (const TCHAR *)use_font.font_path, FA_READ);
	if(use_font.index_to_loc_format == 0)
	{
		if(FR_OK != f_lseek(fp, use_font.loca_start + gid * sizeof(uint16_t)))
			return NULL;
		/* 两个ID offset，一前一后，相当于两个glyph_offset */
		if(FR_OK != f_read(fp, &now, sizeof(uint16_t), &read_length) || sizeof(uint16_t) != read_length)
			return NULL;
		if(FR_OK != f_read(fp, &next, sizeof(uint16_t), &read_length) || sizeof(uint16_t) != read_length)
			return NULL;
	}
	else
	{
		if(FR_OK != f_lseek(fp, use_font.loca_start + gid * sizeof(uint32_t)))
			return NULL;
		/* 两个ID offset，一前一后，相当于两个glyph_offset */
		if(FR_OK != f_read(fp, &now, sizeof(uint32_t), &read_length) || sizeof(uint32_t) != read_length)
			return NULL;
		if(FR_OK != f_read(fp, &next, sizeof(uint32_t), &read_length) || sizeof(uint32_t) != read_length)
			return NULL;
	}

	/* 得到当前字形bitmap的起始地址 */
	now = use_font.glyph_start + now;
	/* 移动文件指针到此处 */
	if(FR_OK != f_lseek(fp, now))
		return NULL;

	/* 初始化指针以及相关参数 */
	use_bit_iterator_t bit_it = use_init_bit_iterator(fp);
	/* 获取字形数据头的大小 */
	int32_t nbits = use_font.advance_width_bits + 2 * use_font.xy_bits + 2 * use_font.wh_bits;
	/* 后一个指针 */
	next = (gid < use_font.loca_count) ? (use_font.glyph_start + next) : use_font.glyph_length;
	/* 得到后一个字形bitmap的起始地址 */
	use_read_bits(&bit_it, nbits, &res);
	if(LV_FS_RES_OK != res)
		return NULL;

	/* bitmap尺寸 */
	int32_t bmp_size = next - now - nbits / 8;

	/* 获取字形bitmap数据 */
	if (nbits % 8 == 0)
	{
		/* 快速读取，当字节对齐的时候可以使用标准的IO进行读取，并保存到glyph_bmp中 */
		if(FR_OK != f_read(fp, use_font.glyph_bitmap, bmp_size, &read_length) || bmp_size != read_length)
			return NULL;
	}
	else
	{
		for (int32_t k = 0; k < bmp_size - 1; ++k)
		{
			use_font.glyph_bitmap[k] = use_read_bits(&bit_it, 8, &res);
			if(LV_FS_RES_OK != res)
				return NULL;
		}
		use_font.glyph_bitmap[bmp_size - 1] = use_read_bits(&bit_it, 8 - nbits % 8, &res);
		if(LV_FS_RES_OK != res)
			return NULL;

		/* 最后一个片段应该在MSB上，但read_bits()将把它放在LSB上 */
		use_font.glyph_bitmap[bmp_size - 1] = use_font.glyph_bitmap[bmp_size - 1] << (nbits % 8);
	}

	/* 字体压缩与解压 */

	if (fdsc->bitmap_format == LV_FONT_FMT_TXT_PLAIN)
		return use_font.glyph_bitmap;
	/* 压缩字体解压 */
	else
	{	/* 不理解这部分 */
#if LV_USE_FONT_COMPRESSED
		/* 局部静态，只初始化一次，运行到此处时候保存为上一次的值 */
		static size_t last_buf_size = 0;

		/* 全局变量_lv_font_decompr_buf，uint8_t *类型 */
		if(LV_GC_ROOT(_lv_font_decompr_buf) == NULL)
			last_buf_size = 0;

		/* box，盒子大小？ */
		uint32_t gsize = use_font.box_w * use_font.box_h;
		if(gsize == 0)
			return NULL;

		/* 需要的实际缓存大小 */
		uint32_t buf_size = gsize;
		/* 计算保存解压缩字形所需的内存大小，取整。 */
		switch(fdsc->bpp)
		{
			case 1:
				buf_size = (gsize + 7) >> 3;
				break;
			case 2:
				buf_size = (gsize + 3) >> 2;
				break;
			case 3:
				buf_size = (gsize + 1) >> 1;
				break;
			case 4:
				buf_size = (gsize + 1) >> 1;
				break;
		}

		/* 跟上一次的缓存大小比较 */
		if(last_buf_size < buf_size)
		{
			uint8_t * tmp = lv_mem_realloc(LV_GC_ROOT(_lv_font_decompr_buf), buf_size);
			LV_ASSERT_MALLOC(tmp);
			if(tmp == NULL)
				return NULL;
			
			LV_GC_ROOT(_lv_font_decompr_buf) = tmp;
			last_buf_size = buf_size;
		}

		/* 预置过滤器 */
		bool prefilter = fdsc->bitmap_format == LV_FONT_FMT_TXT_COMPRESSED ? true : false;
		/* 字体bitmap解压器 */
		use_decompress(use_font.glyph_bitmap, LV_GC_ROOT(_lv_font_decompr_buf), use_font.box_w, use_font.box_h, (uint8_t)fdsc->bpp, prefilter);
		
		/* 返回缓冲区指针，那后面的代码怎么知道大小？ */
		return LV_GC_ROOT(_lv_font_decompr_buf);
#else /*!LV_USE_FONT_COMPRESSED*/
		LV_LOG_WARN("Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled in lv_conf.h");
		return NULL;
#endif
	}

	f_close(fp);
#else
	const lv_font_fmt_txt_glyph_dsc_t * gdsc = &fdsc->glyph_dsc[gid];

	if(fdsc->bitmap_format == LV_FONT_FMT_TXT_PLAIN)
		/* 返回bitmap指针 */
		return &fdsc->glyph_bitmap[gdsc->bitmap_index];
	
	/*处理压缩的位图*/
	else
	{
#if LV_USE_FONT_COMPRESSED
		static size_t last_buf_size = 0;
		if(LV_GC_ROOT(_lv_font_decompr_buf) == NULL)
			last_buf_size = 0;

		uint32_t gsize = gdsc->box_w * gdsc->box_h;
		if(gsize == 0)
			return NULL;

		uint32_t buf_size = gsize;
		/*Compute memory size needed to hold decompressed glyph, rounding up*/
		switch(fdsc->bpp)
		{
			case 1:
				buf_size = (gsize + 7) >> 3;
				break;
			case 2:
				buf_size = (gsize + 3) >> 2;
				break;
			case 3:
				buf_size = (gsize + 1) >> 1;
				break;
			case 4:
				buf_size = (gsize + 1) >> 1;
				break;
		}

		if(last_buf_size < buf_size)
		{
			uint8_t * tmp = lv_mem_realloc(LV_GC_ROOT(_lv_font_decompr_buf), buf_size);
			LV_ASSERT_MALLOC(tmp);
			if(tmp == NULL)
				return NULL;
			LV_GC_ROOT(_lv_font_decompr_buf) = tmp;
			last_buf_size = buf_size;
		}

		bool prefilter = fdsc->bitmap_format == LV_FONT_FMT_TXT_COMPRESSED ? true : false;
		use_decompress(&fdsc->glyph_bitmap[gdsc->bitmap_index], LV_GC_ROOT(_lv_font_decompr_buf), gdsc->box_w, gdsc->box_h, (uint8_t)fdsc->bpp, prefilter);
		return LV_GC_ROOT(_lv_font_decompr_buf);
#else /*!LV_USE_FONT_COMPRESSED*/
		LV_LOG_WARN("Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled in lv_conf.h");
		return NULL;
#endif
	}
#endif

	/*If not returned earlier then the letter is not found in this font*/
	return NULL;
}

/**
 * @brief 获取cmap
 * @note 自动区分win32与fatfs
 * @param fp 文件指针，传入为空指针类型
 * @param font_dsc 字体描述符
 * @param cmap_start cmap起始偏移，由第一个use_read_label读到
 * @return int32_t 返回cmap_length或者-1
 */
static int32_t use_load_cmap(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t cmap_start)
{
	//文件0x30-数据结构
	//58 35 00 00 63 6D 61 70 3D 00 00 00 DC 03 00 00
	//X  5  -  -  c  m  a  p  =  -  -  -  -  -  -  -
	//00 00 00 00 01 00 01 00 01 00 02 00 DC 03 00 00
	//20 00 00 00 5F 00 02 00 5F 00 02 00 DC 03 00 00
	//A2 00 00 00 28 02 61 00 2C 00 03 00 34 04 00 00
	//读取cmap的长度以及判断文件标签，读取长度8字节，即0x30-0x37
	int32_t cmap_length = use_read_label(fp, cmap_start, "cmap");
	if(cmap_length < 0)
		return -1;

	/* 获取cmap子表的数量 */
	/* 读取位置0x38-0x3B，即0x0000003D，共有61个数组 */
	uint32_t cmap_subtables_count;
#if LV_USE_FS_WIN32 != 1
	uint32_t read_length;
	if(FR_OK != f_read((FIL *)fp, &cmap_subtables_count, sizeof(uint32_t), &read_length) || sizeof(uint32_t) != read_length)
#else
	if(lv_fs_read((lv_fs_file_t *)fp, &cmap_subtables_count, sizeof(uint32_t), NULL) != LV_FS_RES_OK)
#endif
		return -1;

	//C文件中的结构
	// static const lv_font_fmt_txt_cmap_t cmaps[] = {
    // {
    //     .range_start = 32, .range_length = 96, .glyph_id_start = 1,
    //     .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    // },
    // {
    //     .range_start = 12289, .range_length = 72, .glyph_id_start = 97,
    //     .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 12, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    // },
	// };
	lv_font_fmt_txt_cmap_t * cmap = lv_mem_alloc(cmap_subtables_count * sizeof(lv_font_fmt_txt_cmap_t));

	memset(cmap, 0, cmap_subtables_count * sizeof(lv_font_fmt_txt_cmap_t));

	/* 将申请到的内存指针保存 */
	font_dsc->cmaps = cmap;
	/* 子表标题数量 */
	font_dsc->cmap_num = cmap_subtables_count;

	use_cmap_table * cmap_tables = lv_mem_alloc(sizeof(use_cmap_table) * font_dsc->cmap_num);

	/* 获取bin文件中的cmap_tables，并存放在font_dsc.cmap中 */
	bool success = use_load_cmaps_tables(fp, font_dsc, cmap_start, cmap_tables);
	
	//最后需要释放掉cmap_tables
	lv_mem_free(cmap_tables);

	return success ? cmap_length : -1;
}

/**
 * @brief 加载cmap表
 * @note 自动区分win32与fatfs
 * @param fp 文件指针
 * @param font_dsc 字体描述符
 * @param cmap_start cmap起始偏移，由第一个use_read_label读到
 * @param cmap_table cmap表指针
 * @return true 
 * @return false 
 * @ref https://github.com/lvgl/lv_font_conv/blob/master/doc/font_spec.md
 */
static bool use_load_cmaps_tables(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t cmap_start, use_cmap_table * cmap_table)
{
	/* 读取61个cmap_table */
#if LV_USE_FS_WIN32 != 1
	uint32_t read_length;
	if(FR_OK != f_read((FIL *)fp, cmap_table, font_dsc->cmap_num * sizeof(use_cmap_table), &read_length) || font_dsc->cmap_num * sizeof(use_cmap_table) != read_length)
#else
	if(lv_fs_read((lv_fs_file_t *)fp, cmap_table, font_dsc->cmap_num * sizeof(use_cmap_table), NULL) != LV_FS_RES_OK)
#endif
		return false;

	/* 分解cmap表到font.dsc中，循环cmap_num次 */
	for(uint16_t i = 0; i < font_dsc->cmap_num; ++i)
	{
		/* 将指针重置到cmap对应的子数据表中 */
#if LV_USE_FS_WIN32 != 1
		if(FR_OK != f_lseek((FIL *)fp, cmap_start + cmap_table[i].data_offset))
#else
		if(LV_FS_RES_OK != lv_fs_seek((lv_fs_file_t *)fp, cmap_start + cmap_table[i].data_offset, LV_FS_SEEK_SET))
#endif
			return false;
		
		/* 指针转换，老麻烦了 */
		lv_font_fmt_txt_cmap_t * cmap = (lv_font_fmt_txt_cmap_t *) &(font_dsc->cmaps[i]);

		/* 将cmap_table中的数据存到cmap中 */
		/* 这个范围的第一个Unicode字符 */
		cmap->range_start = cmap_table[i].range_start;
		/* 与此范围相关的Unicode字符的数量 */
		cmap->range_length = cmap_table[i].range_length;
		/* 第一个字形(“glyph_dsc”的数组索引) */
		cmap->glyph_id_start = cmap_table[i].glyph_id_start;
		/* 字体字符映射表的格式类型 */
		cmap->type = cmap_table[i].format_type;

		/* 字体字符映射表的格式 */
		/* 状态机：根据子表标题中的格式类型决定是否读取子表数据 */
		switch(cmap_table[i].format_type)
		{
			/* 格式0全部 */
			case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL:
            {
                uint8_t ids_size = sizeof(uint8_t) * cmap_table[i].data_entries_count;
                uint8_t* glyph_id_ofs_list = lv_mem_alloc(ids_size);

                /* ????????干嘛用的 */
                cmap->glyph_id_ofs_list = glyph_id_ofs_list;

#if LV_USE_FS_WIN32 != 1
                if (FR_OK != f_read((FIL *)fp, glyph_id_ofs_list, ids_size, &read_length) || ids_size != read_length)
#else
                if (lv_fs_read(fp, glyph_id_ofs_list, ids_size, NULL) != LV_FS_RES_OK)
#endif
                    return false;

                /* “unicode_list”和/或“glyph_id_ofs_list”的长度 */
                cmap->list_length = cmap->range_length;
                break;
            }
			/* 格式0微小 */
			case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
				break;
			/* 格式稀疏 */
			case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
			/* 格式稀疏微小，与格式稀疏完全一样，没有字形ID索引 */
			case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY:
            {
                uint32_t list_size = sizeof(uint16_t) * cmap_table[i].data_entries_count;
                uint16_t* unicode_list = (uint16_t*)lv_mem_alloc(list_size);

                cmap->unicode_list = unicode_list;
                cmap->list_length = cmap_table[i].data_entries_count;

#if LV_USE_FS_WIN32 != 1
                if (FR_OK != f_read((FIL *)fp, unicode_list, list_size, &read_length) || list_size != read_length)
#else
                if (lv_fs_read(fp, unicode_list, list_size, NULL) != LV_FS_RES_OK)
#endif
                    return false;

                /* 格式稀疏含有字形ID索引 */
                if (cmap_table[i].format_type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL)
                {
                    uint16_t* buf = lv_mem_alloc(sizeof(uint16_t) * cmap->list_length);

                    cmap->glyph_id_ofs_list = buf;
#if LV_USE_FS_WIN32 != 1
                    if (FR_OK != f_read((FIL *)fp, buf, sizeof(uint16_t) * cmap->list_length, &read_length) || sizeof(uint16_t) * cmap->list_length != read_length)
#else
                    if (lv_fs_read(fp, buf, sizeof(uint16_t) * cmap->list_length, NULL) != LV_FS_RES_OK)
#endif
                        return false;
                }
                break;
            }
			default:
				LV_LOG_WARN("Unknown cmaps format type %d.", cmap_table[i].format_type);
				return false;
		}
	}
	return true;
}

#if LV_USE_FS_WIN32 == 1
/**
 * @brief 加载bitmap？？？
 * 
 * @param fp 无符号文件指针
 * @param font_dsc 字体描述符指针
 * @param start 偏移地址
 * @param glyph_offset 
 * @param loca_count 
 * @param header 
 * @return int32_t 
 */
static int32_t use_load_glyph(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t start, uint32_t * glyph_offset, uint32_t loca_count, use_font_header * header)
{
	int32_t glyph_length = use_read_label(fp, start, "glyf");
	if(glyph_length < 0)
		return -1;

	lv_font_fmt_txt_glyph_dsc_t * glyph_dsc = (lv_font_fmt_txt_glyph_dsc_t *) lv_mem_alloc(loca_count * sizeof(lv_font_fmt_txt_glyph_dsc_t));

	memset(glyph_dsc, 0, loca_count * sizeof(lv_font_fmt_txt_glyph_dsc_t));

	font_dsc->glyph_dsc = glyph_dsc;

	int cur_bmp_size = 0;

	for(unsigned int i = 0; i < loca_count; ++i)
	{
		lv_font_fmt_txt_glyph_dsc_t * gdsc = &glyph_dsc[i];

		lv_fs_res_t res = lv_fs_seek(fp, start + glyph_offset[i], LV_FS_SEEK_SET);
		if(res != LV_FS_RES_OK)
			return -1;

		use_bit_iterator_t bit_it = use_init_bit_iterator(fp);

		/* 获取字形相关数据 */
		if(header->advance_width_bits == 0)
			/* 默认字形宽度 */
			gdsc->adv_w = header->default_advance_width;
		else
		{
			/* 读取字形数据区的数据 */
			gdsc->adv_w = use_read_bits(&bit_it, header->advance_width_bits, &res);
			if(res != LV_FS_RES_OK)
				return -1;
		}

		/* 高级宽度格式，无符号整型 */
		if(header->advance_width_format == 0)
			gdsc->adv_w *= 16;

		/* 获取offset数据 */
		gdsc->ofs_x = use_read_bits_signed(&bit_it, header->xy_bits, &res);
		if(res != LV_FS_RES_OK)
			return -1;

		gdsc->ofs_y = use_read_bits_signed(&bit_it, header->xy_bits, &res);
		if(res != LV_FS_RES_OK)
			return -1;

		/* 获取box数据 */
		gdsc->box_w = use_read_bits(&bit_it, header->wh_bits, &res);
		if(res != LV_FS_RES_OK)
			return -1;

		gdsc->box_h = use_read_bits(&bit_it, header->wh_bits, &res);
		if(res != LV_FS_RES_OK)
			return -1;

		/* 获取数据bitmap大小？ */
		int nbits = header->advance_width_bits + 2 * header->xy_bits + 2 * header->wh_bits;
		int next_offset = (i < loca_count - 1) ? glyph_offset[i + 1] : (uint32_t)glyph_length;
		int bmp_size = next_offset - glyph_offset[i] - nbits / 8;

		if(i == 0)
		{
			gdsc->adv_w = 0;
			gdsc->box_w = 0;
			gdsc->box_h = 0;
			gdsc->ofs_x = 0;
			gdsc->ofs_y = 0;
		}

		gdsc->bitmap_index = cur_bmp_size;
		if(gdsc->box_w * gdsc->box_h != 0)
			/* 累计的bitmap大小 */
			cur_bmp_size += bmp_size;
	}
	/* 使用累计的bitmap大小申请内存来存放所有的字体数据 */
	uint8_t * glyph_bmp = (uint8_t *)lv_mem_alloc(sizeof(uint8_t) * cur_bmp_size);
	/* 保存字体数据 */
	font_dsc->glyph_bitmap = glyph_bmp;

	cur_bmp_size = 0;

	for(unsigned int i = 1; i < loca_count; ++i)
	{

		lv_fs_res_t res = lv_fs_seek(fp, start + glyph_offset[i], LV_FS_SEEK_SET);
		if(res != LV_FS_RES_OK)
			return -1;

		/* 重置指针 */
		use_bit_iterator_t bit_it = use_init_bit_iterator(fp);

		/* 计算前面的数据大小 */
		int nbits = header->advance_width_bits + 2 * header->xy_bits + 2 * header->wh_bits;

		/* 读取字形相关数据，丢弃，没啥用，只是单纯的移动指针到数据区域 */
		use_read_bits(&bit_it, nbits, &res);
		if(res != LV_FS_RES_OK)
			return -1;

		/* 意味着字体没有数据，提前退出本次循环 */
		if(glyph_dsc[i].box_w * glyph_dsc[i].box_h == 0)
			continue;

		/* 下一个字形数据的偏移(bin文件中的偏移值) */
		int next_offset = (i < loca_count - 1) ? glyph_offset[i + 1] : (uint32_t)glyph_length;
		/* 得到字形bitmap数据区的大小 */
		int bmp_size = next_offset - glyph_offset[i] - nbits / 8;

		/* nbits刚好是整bit数，因为后面的字形bitmap数据是字节对齐的 */
		if(nbits % 8 == 0)
		{
			/* 快速读取，当字节对齐的时候可以使用标准的IO进行读取，并保存到glyph_bmp中 */
			if(lv_fs_read(fp, &glyph_bmp[cur_bmp_size], bmp_size, NULL) != LV_FS_RES_OK)
				return -1;
		}
		else
		{
			/* 开始读取复制字形bitmap的数据 */
			for(int k = 0; k < bmp_size - 1; ++k)
			{
				glyph_bmp[cur_bmp_size + k] = use_read_bits(&bit_it, 8, &res);
				if(res != LV_FS_RES_OK)
					return -1;
			}
			glyph_bmp[cur_bmp_size + bmp_size - 1] = use_read_bits(&bit_it, 8 - nbits % 8, &res);
			if(res != LV_FS_RES_OK)
				return -1;

			/* 最后一个片段应该在MSB上，但read_bits()将把它放在LSB上 */
			glyph_bmp[cur_bmp_size + bmp_size - 1] = glyph_bmp[cur_bmp_size + bmp_size - 1] << (nbits % 8);
		}

		/* 指针增加 */
		cur_bmp_size += bmp_size;
	}
	return glyph_length;
}
#endif

/**
 * @brief 
 * 
 * @param fp 
 * @param font_dsc 
 * @param format 
 * @param start 
 * @return int32_t 
 */
static int32_t use_load_kern(void * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint8_t format, uint32_t kern_start)
{
	int32_t kern_length = use_read_label(fp, kern_start, "kern");
	if(kern_length < 0)
		return -1;

	uint8_t kern_format_type;
	int32_t padding;

#if LV_USE_FS_WIN32 != 1
	uint32_t read_length;
	if(FR_OK != f_read((FIL *)fp, &kern_format_type, sizeof(uint8_t), &read_length) || sizeof(uint8_t) != read_length ||
	   FR_OK != f_read((FIL *)fp, &padding, 3 * sizeof(uint8_t), &read_length))
#else
	if(LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &kern_format_type, sizeof(uint8_t), NULL) || LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &padding, 3 * sizeof(uint8_t), NULL))
#endif
		return -1;

	/* 两种格式，格式0和格式3 */
	if(0 == kern_format_type)	/*排序对*/
	{
		lv_font_fmt_txt_kern_pair_t * kern_pair = lv_mem_alloc(sizeof(lv_font_fmt_txt_kern_pair_t));

		memset(kern_pair, 0, sizeof(lv_font_fmt_txt_kern_pair_t));

		font_dsc->kern_dsc = kern_pair;
		font_dsc->kern_classes = 0;

		uint32_t glyph_entries;
#if LV_USE_FS_WIN32 != 1
		if(FR_OK != f_read((FIL *)fp, &glyph_entries, sizeof(uint32_t), &read_length) || sizeof(uint32_t) != read_length)
#else
		if(LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &glyph_entries, sizeof(uint32_t), NULL))
#endif
			return -1;

		int ids_size;
		if(format == 0)
			ids_size = sizeof(int8_t) * 2 * glyph_entries;
		else
			ids_size = sizeof(int16_t) * 2 * glyph_entries;

		uint8_t * glyph_ids = lv_mem_alloc(ids_size);
		int8_t * values = lv_mem_alloc(glyph_entries);

		kern_pair->glyph_ids_size = format;
		kern_pair->pair_cnt = glyph_entries;
		kern_pair->glyph_ids = glyph_ids;
		kern_pair->values = values;

#if LV_USE_FS_WIN32 != 1
		if(FR_OK != f_read((FIL *)fp, glyph_ids, ids_size, &read_length) || ids_size != read_length)
#else
		if(LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, glyph_ids, ids_size, NULL))
#endif
			return -1;

#if LV_USE_FS_WIN32 != 1
		if(FR_OK != f_read((FIL *)fp, values, glyph_entries, &read_length) || glyph_entries != read_length)
#else
		if(LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, values, glyph_entries, NULL))
#endif
			return -1;
	}
	else
	{
		if(3 == kern_format_type)	/*数组M*N类*/
		{

			lv_font_fmt_txt_kern_classes_t * kern_classes = lv_mem_alloc(sizeof(lv_font_fmt_txt_kern_classes_t));

			memset(kern_classes, 0, sizeof(lv_font_fmt_txt_kern_classes_t));

			font_dsc->kern_dsc = kern_classes;
			font_dsc->kern_classes = 1;

			uint16_t kern_class_mapping_length;
			uint8_t kern_table_rows;
			uint8_t kern_table_cols;

#if LV_USE_FS_WIN32 != 1
			if( FR_OK != f_read((FIL *)fp, &kern_class_mapping_length, sizeof(uint16_t), &read_length) ||
				FR_OK != f_read((FIL *)fp, &kern_table_rows, sizeof(uint8_t), &read_length) ||
				FR_OK != f_read((FIL *)fp, &kern_table_cols, sizeof(uint8_t), &read_length))
#else
			if( LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &kern_class_mapping_length, sizeof(uint16_t), NULL) ||
				LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &kern_table_rows, sizeof(uint8_t), NULL) ||
				LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, &kern_table_cols, sizeof(uint8_t), NULL))
#endif
				return -1;

			int kern_values_length = sizeof(int8_t) * kern_table_rows * kern_table_cols;

			uint8_t * kern_left = lv_mem_alloc(kern_class_mapping_length);
			uint8_t * kern_right = lv_mem_alloc(kern_class_mapping_length);
			int8_t * kern_values = lv_mem_alloc(kern_values_length);

			kern_classes->left_class_mapping  = kern_left;
			kern_classes->right_class_mapping = kern_right;
			kern_classes->left_class_cnt = kern_table_rows;
			kern_classes->right_class_cnt = kern_table_cols;
			kern_classes->class_pair_values = kern_values;

#if LV_USE_FS_WIN32 != 1
			if( FR_OK != f_read((FIL *)fp, kern_left, kern_class_mapping_length, &read_length) ||
				FR_OK != f_read((FIL *)fp, kern_right, kern_class_mapping_length, &read_length) ||
				FR_OK != f_read((FIL *)fp, kern_values, kern_values_length, &read_length))
#else
			if( LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, kern_left, kern_class_mapping_length, NULL) ||
				LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, kern_right, kern_class_mapping_length, NULL) ||
				LV_FS_RES_OK != lv_fs_read((lv_fs_file_t *)fp, kern_values, kern_values_length, NULL))
#endif
				return -1;
		}
		else
		{
			LV_LOG_WARN("Unknown kern_format_type: %d", kern_format_type);
			return -1;
		}
	}

	return kern_length;
}

/**
 * @brief 获取Flash中的loca ID偏移量，即glyph offset
 * @note 需要根据自己设备修改
 * @param font
 * @param gid
 * @param gdsc
 * @return bool
 */
#if LV_USE_FS_WIN32 != 1
static bool use_get_glyph_dsc(const lv_font_t * font, uint32_t gid, lv_font_fmt_txt_glyph_dsc_t *gdsc)
{
	lv_fs_res_t res;
	/**********这里需要一个全局的指针*************/
	FIL * fp = use_font.fp;
	uint32_t read_length;

	/* 索引loca中的ID偏移值 */
	uint32_t now;
	/* 打开文件 */
	f_open(fp, (const TCHAR *)use_font.font_path, FA_READ);
	/* 索引到位置表中的格式( 0 - Offset16, 1 - Offset32) */
	if(use_font.index_to_loc_format == 0)
	{
		/* 设置指针位置 */
		f_lseek(fp, use_font.loca_start + gid * sizeof(uint16_t));
		/* 读取loca中的ID偏移数据，注意loca起始需要偏移12，因为loca_length、loca、loca_count占去了12bytes */
		/* 偏移数据保存在now中 */
		f_read(fp, &now, sizeof(uint16_t), &read_length);
		if(sizeof(uint16_t) != read_length)
			return -1;
	}
	else
	{
		f_lseek(fp, use_font.loca_start + gid * sizeof(uint32_t));
		f_read(fp, &now, sizeof(uint32_t), &read_length);
		if(sizeof(uint32_t) != read_length)
			return -1;
	}
	/* 重设指针至指定偏移处 */
	f_lseek(fp, use_font.glyph_start + now);

	/* 用来重置偏移 */
	use_bit_iterator_t bit_it = use_init_bit_iterator(fp);

	if(use_font.advance_width_bits == 0)
		gdsc->adv_w = use_font.default_advance_width;
	else
	{
		gdsc->adv_w = use_read_bits(&bit_it, use_font.advance_width_bits, &res);
		if(FR_OK != res)
			return -1;
	}

	if(use_font.advance_width_format == 0)
		gdsc->adv_w *= 16;

	gdsc->ofs_x = use_read_bits_signed(&bit_it, use_font.xy_bits, &res);
	if(FR_OK != res)
		return -1;
	gdsc->ofs_y = use_read_bits_signed(&bit_it, use_font.xy_bits, &res);
	if(FR_OK != res)
		return -1;
	gdsc->box_w = use_read_bits(&bit_it, use_font.wh_bits, &res);
	if(FR_OK != res)
		return -1;
	gdsc->box_h = use_read_bits(&bit_it, use_font.wh_bits, &res);
	if(FR_OK != res)
		return -1;
	
	/* 关闭文件 */
	f_close(fp);
	return true;
}
#endif

/**
 * @brief 
 * @note 不需要修改，直接使用
 * @param font 
 * @param letter 
 * @return uint32_t 
 */
static uint32_t use_get_glyph_dsc_id(const lv_font_t * font, uint32_t letter)
{
	if(letter == '\0')
		return 0;

	lv_font_fmt_txt_dsc_t * fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;

	/*Check the cache first*/
	if(fdsc->cache && letter == fdsc->cache->last_letter)
		return fdsc->cache->last_glyph_id;

	uint16_t i;
	for(i = 0; i < fdsc->cmap_num; i++)
	{
		/*Relative code point*/
		uint32_t rcp = letter - fdsc->cmaps[i].range_start;
		if(rcp > fdsc->cmaps[i].range_length)
			continue;

		uint32_t glyph_id = 0;
		if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY)
			glyph_id = fdsc->cmaps[i].glyph_id_start + rcp;
		else
		{
			if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL)
			{
				const uint8_t * gid_ofs_8 = fdsc->cmaps[i].glyph_id_ofs_list;
				glyph_id = fdsc->cmaps[i].glyph_id_start + gid_ofs_8[rcp];
			}
			else
			{
				if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY)
				{
					uint16_t key = rcp;
					uint16_t * p = _lv_utils_bsearch(&key, fdsc->cmaps[i].unicode_list, fdsc->cmaps[i].list_length, sizeof(fdsc->cmaps[i].unicode_list[0]), use_unicode_list_compare);

					if(p)
					{
						lv_uintptr_t ofs = p - fdsc->cmaps[i].unicode_list;
						glyph_id = fdsc->cmaps[i].glyph_id_start + ofs;
					}
				}
				else
				{
					if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL)
					{
						uint16_t key = rcp;
						uint16_t * p = _lv_utils_bsearch(&key, fdsc->cmaps[i].unicode_list, fdsc->cmaps[i].list_length, sizeof(fdsc->cmaps[i].unicode_list[0]), use_unicode_list_compare);

						if(p)
						{
							lv_uintptr_t ofs = p - fdsc->cmaps[i].unicode_list;
							const uint16_t * gid_ofs_16 = fdsc->cmaps[i].glyph_id_ofs_list;
							glyph_id = fdsc->cmaps[i].glyph_id_start + gid_ofs_16[ofs];
						}
					}
				}
			}
		}
		/*Update the cache*/
		if(fdsc->cache)
		{
			fdsc->cache->last_letter = letter;
			fdsc->cache->last_glyph_id = glyph_id;
		}
		return glyph_id;
	}

	if(fdsc->cache)
	{
		fdsc->cache->last_letter = letter;
		fdsc->cache->last_glyph_id = 0;
	}
	return 0;
}

/**
 * @brief 获取kern值对象
 * @note 
 * @param font 
 * @param gid_left 
 * @param gid_right 
 * @return int8_t 
 */
static int8_t use_get_kern_value(const lv_font_t * font, uint32_t gid_left, uint32_t gid_right)
{
	lv_font_fmt_txt_dsc_t * fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;

	int8_t value = 0;

	if(fdsc->kern_classes == 0)
	{
		/*Kern pairs*/
		const lv_font_fmt_txt_kern_pair_t * kdsc = fdsc->kern_dsc;
		if(kdsc->glyph_ids_size == 0)
		{
			/*Use binary search to find the kern value.
			 *The pairs are ordered left_id first, then right_id secondly.*/
			const uint16_t * g_ids = kdsc->glyph_ids;
			uint16_t g_id_both = (gid_right << 8) + gid_left; /*Create one number from the ids*/
			uint16_t * kid_p = _lv_utils_bsearch(&g_id_both, g_ids, kdsc->pair_cnt, 2, use_kern_pair_8_compare);

			/*If the `g_id_both` were found get its index from the pointer*/
			if(kid_p)
			{
				lv_uintptr_t ofs = kid_p - g_ids;
				value = kdsc->values[ofs];
			}
		}
		else
		{
			if(kdsc->glyph_ids_size == 1)
			{
				/*Use binary search to find the kern value.
				*The pairs are ordered left_id first, then right_id secondly.*/
				const uint32_t * g_ids = kdsc->glyph_ids;
				uint32_t g_id_both = (gid_right << 16) + gid_left; /*Create one number from the ids*/
				uint32_t * kid_p = _lv_utils_bsearch(&g_id_both, g_ids, kdsc->pair_cnt, 4, use_kern_pair_16_compare);

				/*If the `g_id_both` were found get its index from the pointer*/
				if(kid_p)
				{
					lv_uintptr_t ofs = kid_p - g_ids;
					value = kdsc->values[ofs];
				}

			}
			else
			{
				/*Invalid value*/
			}
		}
	}
	else
	{
		/*Kern classes*/
		const lv_font_fmt_txt_kern_classes_t * kdsc = fdsc->kern_dsc;
		uint8_t left_class = kdsc->left_class_mapping[gid_left];
		uint8_t right_class = kdsc->right_class_mapping[gid_right];

		/*If class = 0, kerning not exist for that glyph
		 *else got the value form `class_pair_values` 2D array*/
		if(left_class > 0 && right_class > 0)
			value = kdsc->class_pair_values[(left_class - 1) * kdsc->right_class_cnt + (right_class - 1)];

	}
	return value;
}

/** 
 * @brief 比较两个输入参数的值
 *
 * @param[in]  ref        Pointer to the reference.
 * @param[in]  element    Pointer to the element to compare.
 *
 * @return Result of comparison.
 * @retval < 0   Reference is less than element.
 * @retval = 0   Reference is equal to element.
 * @retval > 0   Reference is greater than element.
 */
static int32_t use_unicode_list_compare(const void * ref, const void * element)
{
    return ((int32_t)(*(uint16_t *)ref)) - ((int32_t)(*(uint16_t *)element));
}

/**
 * @brief 
 * 
 * @param ref 
 * @param element 
 * @return int32_t 
 */
static int32_t use_kern_pair_8_compare(const void * ref, const void * element)
{
    const uint8_t * ref8_p = ref;
    const uint8_t * element8_p = element;

    /*If the MSB is different it will matter. If not return the diff. of the LSB*/
    if(ref8_p[0] != element8_p[0])
		return (int32_t)ref8_p[0] - element8_p[0];
    else
		return (int32_t) ref8_p[1] - element8_p[1];

}

/**
 * @brief 
 * 
 * @param ref 
 * @param element 
 * @return int32_t 
 */
static int32_t use_kern_pair_16_compare(const void * ref, const void * element)
{
    const uint16_t * ref16_p = ref;
    const uint16_t * element16_p = element;

    /*If the MSB is different it will matter. If not return the diff. of the LSB*/
    if(ref16_p[0] != element16_p[0])
		return (int32_t)ref16_p[0] - element16_p[0];
    else
		return (int32_t) ref16_p[1] - element16_p[1];
}

/**
 * @brief 有什么用？？？
 * 
 * @param fp 
 * @return use_bit_iterator_t 
 */
static use_bit_iterator_t use_init_bit_iterator(void * fp)
{
	use_bit_iterator_t it;
	it.fp = fp;
	it.bit_pos = -1;
	it.byte_value = 0;

	return it;
}

/**
 * @brief 读取n个bit的数据，并返回
 * @note 数据的大小在4个字节以内
 * @param it 结构指针，包含文件指针和比特流值
 * @param n_bits 要读取的比特数
 * @param res 操作结果
 * @return 读取的数据
 */
static uint32_t use_read_bits(use_bit_iterator_t * it, int n_bits, lv_fs_res_t * res)
{
	uint32_t value = 0;
	while(n_bits--)
	{
		/* 保存读取数据使用的，大小是1 byte */
		it->byte_value = it->byte_value << 1;
		it->bit_pos--;

		/* 每1个字节读取一次数据？ */
		if(it->bit_pos < 0)
		{
			it->bit_pos = 7;
#if LV_USE_FS_WIN32 != 1
			uint32_t read_length;
			/* 读取一个字节数据，并分出n个bit */
			*res = f_read(it->fp, &(it->byte_value), 1, &read_length);
#else
			*res = lv_fs_read(it->fp, &(it->byte_value), 1, NULL);
#endif
			if(*res != LV_FS_RES_OK)
				return 0;
		}
		int8_t bit = (it->byte_value & 0x80) ? 1 : 0;

		value |= (bit << n_bits);
	}
	*res = LV_FS_RES_OK;
	return value;
}

/**
 * @brief 
 * 
 * @param it 
 * @param n_bits 
 * @param res 
 * @return 
 */
static int32_t use_read_bits_signed(use_bit_iterator_t * it, int n_bits, lv_fs_res_t * res)
{
	uint32_t value = use_read_bits(it, n_bits, res);
	if(value & (1 << (n_bits - 1)))
		value |= ~0u << n_bits;

	return value;
}

/************************************************ 字体bitmap解压 ************************************************/

#if LV_USE_FONT_COMPRESSED
/**
 * @brief 压缩字形的位图
 * @param in 压缩位图指针
 * @param out 存储结果的缓冲区指针
 * @param px_num 字形中的像素数(宽度*高度)
 * @param bpp 位/像素(bpp = 3将被转换为bpp = 4)
 * @param prefilter 输入为真时，行是XORed
 * @return void
 */
static void use_decompress(const uint8_t * in, uint8_t * out, lv_coord_t w, lv_coord_t h, uint8_t bpp, bool prefilter)
{
    uint32_t wrp = 0;
    uint8_t wr_size = bpp;

	/* bpp转换 */
    if(bpp == 3)
		wr_size = 4;

    use_rle_init(in, bpp);

    uint8_t * line_buf1 = lv_mem_buf_get(w);

    uint8_t * line_buf2 = NULL;

    if(prefilter)
        line_buf2 = lv_mem_buf_get(w);

    use_decompress_line(line_buf1, w);

    lv_coord_t y;
    lv_coord_t x;

    for(x = 0; x < w; x++)
	{
        use_bits_write(out, wrp, line_buf1[x], bpp);
        wrp += wr_size;
    }

    for(y = 1; y < h; y++)
	{
        if(prefilter)
		{
            use_decompress_line(line_buf2, w);

            for(x = 0; x < w; x++)
			{
                line_buf1[x] = line_buf2[x] ^ line_buf1[x];
                use_bits_write(out, wrp, line_buf1[x], bpp);
                wrp += wr_size;
            }
        }
        else
		{
            use_decompress_line(line_buf1, w);

            for(x = 0; x < w; x++)
			{
                use_bits_write(out, wrp, line_buf1[x], bpp);
                wrp += wr_size;
            }
        }
    }

	/* 释放缓冲区 */
    lv_mem_buf_release(line_buf1);
    lv_mem_buf_release(line_buf2);
}

/**
 * @brief 解压一行，每个字节存储一个像素
 * @note 内联函数
 * @param out 输出缓冲区
 * @param w 行宽度(以像素计)
 */
static inline void use_decompress_line(uint8_t * out, lv_coord_t w)
{
    lv_coord_t i;
    for(i = 0; i < w; i++)
        out[i] = use_rle_next();
}

/**
 * 从输入缓冲区中读取位。读取可以跨字节边界。
 * @param in 要读取的输入缓冲区。
 * @param bit_pos 要读取的第一个位的索引。
 * @param len 要读取的比特数(必须<= 8)。
 * @return 读取比特数
 */
static inline uint8_t use_get_bits(const uint8_t * in, uint32_t bit_pos, uint8_t len)
{
    uint8_t bit_mask;
    switch(len)
	{
        case 1:
            bit_mask = 0x1;
            break;
        case 2:
            bit_mask = 0x3;
            break;
        case 3:
            bit_mask = 0x7;
            break;
        case 4:
            bit_mask = 0xF;
            break;
        case 8:
            bit_mask = 0xFF;
            break;
        default:
            bit_mask = (uint16_t)((uint16_t) 1 << len) - 1;
    }

    uint32_t byte_pos = bit_pos >> 3;
    bit_pos = bit_pos & 0x7;

    if(bit_pos + len >= 8)
	{
        uint16_t in16 = (in[byte_pos] << 8) + in[byte_pos + 1];
        return (in16 >> (16 - bit_pos - len)) & bit_mask;
    }
    else
        return (in[byte_pos] >> (8 - bit_pos - len)) & bit_mask;
}

/**
 * @brief 将'val'数据写入'out'的'bit_pos'位置。写不能跨字节边界。
 * @param out 写入缓冲区
 * @param bit_pos 写入位索引
 * @param val value to write
 * @param len 从'val'写入的位的长度。(从LSB计数)。
 * @note 'len == 3'将被转换为'len = 4'，'val'也将被放大。内联函数
 */
static inline void use_bits_write(uint8_t * out, uint32_t bit_pos, uint8_t val, uint8_t len)
{
    if(len == 3)
	{
        len = 4;
        switch(val)
		{
            case 0:
                val = 0;
                break;
            case 1:
                val = 2;
                break;
            case 2:
                val = 4;
                break;
            case 3:
                val = 6;
                break;
            case 4:
                val = 9;
                break;
            case 5:
                val = 11;
                break;
            case 6:
                val = 13;
                break;
            case 7:
                val = 15;
                break;
        }
    }

    uint16_t byte_pos = bit_pos >> 3;
    bit_pos = bit_pos & 0x7;
    bit_pos = 8 - bit_pos - len;

    uint8_t bit_mask = (uint16_t)((uint16_t) 1 << len) - 1;
    out[byte_pos] &= ((~bit_mask) << bit_pos);
    out[byte_pos] |= (val << bit_pos);
}

static inline void use_rle_init(const uint8_t * in,  uint8_t bpp)
{
    use_rle_in = in;
    use_rle_bpp = bpp;
    use_rle_state = RLE_STATE_SINGLE;
    use_rle_rdp = 0;
    use_rle_prev_v = 0;
    use_rle_cnt = 0;
}

static inline uint8_t use_rle_next(void)
{
    uint8_t v = 0;
    uint8_t ret = 0;

    if(use_rle_state == RLE_STATE_SINGLE)
	{
        ret = use_get_bits(use_rle_in, use_rle_rdp, use_rle_bpp);
        if(use_rle_rdp != 0 && use_rle_prev_v == ret)
		{
            use_rle_cnt = 0;
            use_rle_state = RLE_STATE_REPEATE;
        }

        use_rle_prev_v = ret;
        use_rle_rdp += use_rle_bpp;
    }
    else
	{
		if(use_rle_state == RLE_STATE_REPEATE)
		{
			v = use_get_bits(use_rle_in, use_rle_rdp, 1);
			use_rle_cnt++;
			use_rle_rdp += 1;
			if(v == 1)
			{
				ret = use_rle_prev_v;
				if(use_rle_cnt == 11)
				{
					use_rle_cnt = use_get_bits(use_rle_in, use_rle_rdp, 6);
					use_rle_rdp += 6;
					if(use_rle_cnt != 0)
						use_rle_state = RLE_STATE_COUNTER;
					else
					{
						ret = use_get_bits(use_rle_in, use_rle_rdp, use_rle_bpp);
						use_rle_prev_v = ret;
						use_rle_rdp += use_rle_bpp;
						use_rle_state = RLE_STATE_SINGLE;
					}
				}
			}
			else
			{
				ret = use_get_bits(use_rle_in, use_rle_rdp, use_rle_bpp);
				use_rle_prev_v = ret;
				use_rle_rdp += use_rle_bpp;
				use_rle_state = RLE_STATE_SINGLE;
			}

		}
		else
		{
			if(use_rle_state == RLE_STATE_COUNTER)
			{
				ret = use_rle_prev_v;
				use_rle_cnt--;
				if(use_rle_cnt == 0)
				{
					ret = use_get_bits(use_rle_in, use_rle_rdp, use_rle_bpp);
					use_rle_prev_v = ret;
					use_rle_rdp += use_rle_bpp;
					use_rle_state = RLE_STATE_SINGLE;
				}
			}
		}
	}

    return ret;
}
#endif /*LV_USE_FONT_COMPRESSED*/

