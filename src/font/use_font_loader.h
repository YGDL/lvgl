#ifndef __USE_FONT_LOADER
#define __USE_FONT_LOADER

#ifdef __cplusplus
extern "C" {
#endif

#include "../lv_conf.h"
#include "../misc/lv_fs.h"
#include "lv_font.h"
#include "stdint.h"
#include "stdbool.h"

#if LV_USE_FS_WIN32 != 1
#include "ff.h"
#define USE_FS_LETTER 'S'
#endif

/**
 * @brief 解压器的枚举
 * 
 */
typedef enum {
    RLE_STATE_SINGLE = 0,
    RLE_STATE_REPEATE,
    RLE_STATE_COUNTER,
}use_rle_state_t;

/**
 * @brief 不知道干嘛用的
 * 
 */
typedef struct {
#if LV_USE_FS_WIN32 != 1
	FIL * fp;				/* 小内存的MCU使用 */
#else
    lv_fs_file_t * fp;		/* WIN32模拟器使用 */
#endif
    int8_t bit_pos;
    uint8_t byte_value;
}use_bit_iterator_t;

/**
 * @brief 字体参数结构
 * 
 */
typedef struct __use_font_header {
	uint32_t version;				/**< 字体版本号 */
	uint16_t tables_count;			/**< 附加表的数量 */
	uint16_t font_size;				/**< 字体大小(px) */
	uint16_t ascent;				/**< 上升？？？ */
	int16_t descent;				/**< 下降？？？ */
	uint16_t typo_ascent;			/**< 错别字上升，排版上升 */
	int16_t typo_descent;			/**< 错别字下降，排版下降 */
	uint16_t typo_line_gap;			/**< 错别字行间距，排版行间距 */
	int16_t min_y;					/**< 最小Y值(用于快速检查与其他对象相交的线) */
	int16_t max_y;					/**< 最大Y值 */
	uint16_t default_advance_width;	/**< 默认前进宽度(uint16)，如果字形前进宽度位长度为0 */
	uint16_t kerning_scale;			/**< 字距调整比例，FP12.4无符号，用于字距调整数据的比例，以1字节容纳源 */
	uint8_t index_to_loc_format;	/**< 索引到位置表中的格式( 0 - Offset16, 1 - Offset32) */
	uint8_t glyph_id_format;		/**< 字形ID格式( 0 - 1 byte, -2 bytes) */
	uint8_t advance_width_format;	/**< 高级宽度格式( 0 - 无符号整型, 1 - 无符号，带4位小数部分) */
	uint8_t bits_per_pixel;			/**< 每像素位数(1、2、3或4) */
	uint8_t xy_bits;				/**< 字形BBox x/y位长度(无符号) */
	uint8_t wh_bits;				/**< 字形BBox w/h位长度(无符号) */
	uint8_t advance_width_bits;		/**< 字形前进宽度位长度(无符号，可能是FP4) */
	uint8_t compression_id;			/**< 压缩算法ID(0 - 原始位，1 - 类似RLE的XOR预过滤器，2 - 仅无预过滤器的类似RLE) */
	uint8_t subpixels_mode;			/**< 子像素渲染。(0 - 无，1 - 位图的亮度分辨率为3x，2 - 位图的垂直分辨率为3x) */
	uint8_t padding;				/**< 保留(对齐到2x) */
	int16_t underline_position;		/**< 下划线位置，缩放后post.underlinePosition */
	uint16_t underline_thickness;	/**< 下划线厚度，缩放后post.underlineThickness */
}use_font_header;

/**
 * @brief 字体bin文件中的cmap表数据结构
 * 
 */
typedef struct __use_cmap_table {
    uint32_t data_offset;			/**<  */
    uint32_t range_start;			/**<  */
    uint16_t range_length;			/**<  */
    uint16_t glyph_id_start;		/**<  */
    uint16_t data_entries_count;	/**<  */
    uint8_t format_type;			/**<  */
    uint8_t padding;				/**<  */
}use_cmap_table;

#if LV_USE_FS_WIN32 != 1
/**
 * @brief 自定义文件指针
 * 
 * @details 该指针是精简的文件指针，内部包含了文件只针对的起始地址以及现在的指针地址
 */
typedef struct __use_file_t {
	/*! @brief 精简文件指针 */
	void * fp_start;
    void * fp;
}use_file_t;

/**
 * @brief 用户管理的字体库数据
 * @note 按4字节对齐，注意，指针为4字节长度
 */
typedef struct __use_font_data {
	uint8_t index_to_loc_format;	/**< 索引到位置表中的格式( 0 - Offset16, 1 - Offset32) */
	uint8_t advance_width_bits;		/**< 字形前进宽度位长度(无符号，可能是FP4) */
	uint8_t advance_width_format;	/**< 高级宽度格式(0 - 无符号整型, 1 - 无符号，带4位小数部分) */
	uint8_t xy_bits;				/**< 字形BBox x/y位长度(无符号) */
	uint8_t wh_bits;				/**< 字形BBox w/h位长度(无符号) */
	uint8_t padding;				/**< 填补，按4字节对齐 */
	uint8_t box_w;					/**< 字体框宽 */
	uint8_t box_h;					/**< 字体框高 */
	int8_t ofs_x;					/**< 字体框x偏移 */
	int8_t ofs_y;					/**< 字体框y偏移 */
	uint16_t default_advance_width;	/**< 默认前进宽度(uint16)，如果字形前进宽度位长度为0 */
	uint32_t loca_start;
	uint32_t loca_length;
	uint32_t loca_count;
	uint32_t glyph_start;
	uint32_t glyph_length;
	uint8_t * glyph_bitmap;
	FIL * fp;
	const char * font_path;
}use_font_data;

/**
 * @brief NAND FLASH操作IO句柄结构
 * 
 */
typedef struct __use_nand_io {
    /*! @brief 驱动器号 */
    uint8_t letter;
    /*! @brief 打开文件，调用FatFs */
    FRESULT (* open)(use_file_t *, TCHAR *);
	/*! @brief 读取NAND FLASH的指定数据到缓存中，返回操作结果 */
    FRESULT (* read)(use_file_t *, void *, uint32_t);
	/*! @brief 设置自定义文件指针位置 */
	FRESULT (* seek)(use_file_t *, uint32_t);
	/*! @brief 关闭已经打开的文件，调用FatFs */
	FRESULT (* close)(use_file_t *);
}use_fs_io;
#endif

// /**
//  * @brief 用于FS的文件结构与非FS的文件结构
//  * 
//  * @details 
//  */
// typedef struct __use_fs_file_t {
// #if LV_USE_FS_WIN32 != 1
//     /*! @brief 精简文件指针 */
// 	void * fp_start;
//     void * fp;
// #else
//     /*! @brief FS的文件指针 */
//     lv_fs_file_t fp;
// #endif
// }use_fs_file_t;

//外部函数
lv_font_t * use_font_load(const char * font_name);
void use_font_free(lv_font_t * font);
bool use_font_get_glyph_dsc_fmt_txt(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next);
const uint8_t * use_font_get_bitmap_fmt_txt(const lv_font_t * font, uint32_t unicode_letter);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
