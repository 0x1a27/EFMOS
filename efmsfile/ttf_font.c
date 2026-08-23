/* ttf_font.c - TTF 字体加载器与栅格化器
 * #include 此文件到 graphics_drv.c 中编译
 *
 * 实现: TTF 表解析 + 二次贝塞尔展平 + 扫描线栅格化 (2x2 SSAA) + 字形缓存
 */
#include "ttf_font.h"

/* ==================== 内存/IO 回调 ==================== */
static void *(*s_malloc)(unsigned long) = 0;
static void  (*s_free)(void *) = 0;
static int   (*s_read)(const char *, char *, int) = 0;
static void  (*s_log)(const char *) = 0;

void ttf_set_alloc(void *(*malloc_fn)(unsigned long),
                   void  (*free_fn)(void *),
                   int   (*read_fn)(const char *, char *, int),
                   void  (*log_fn)(const char *)) {
    s_malloc = malloc_fn;
    s_free   = free_fn;
    s_read   = read_fn;
    s_log    = log_fn;
}

#define TTF_LOG(s)  do { if (s_log) s_log(s); } while(0)

/* ==================== 大端读取 ==================== */
static unsigned int rd_u16(const unsigned char *p) {
    return ((unsigned int)p[0] << 8) | p[1];
}
static unsigned int rd_u32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}
static int rd_i16(const unsigned char *p) {
    return (short)((p[0] << 8) | p[1]);
}

/* ==================== TTF 字体上下文 ==================== */
#define TTF_MAX_TABLES 32

struct ttf_table_loc {
    char tag[5];       /* 4 字节标签 + NUL */
    unsigned int offset;
    unsigned int length;
};

struct ttf_font {
    unsigned char *data;      /* 完整字体文件数据 */
    int data_size;

    /* 表位置 */
    struct ttf_table_loc tables[TTF_MAX_TABLES];
    int num_tables;

    /* 解析后的值 */
    unsigned short units_per_em;
    unsigned short num_glyphs;
    short loca_format;       /* 0=short, 1=long */
    unsigned short num_hmetrics;
    short ascent;            /* hhea.ascent (font units) */
    short descent;           /* hhea.descent (font units, negative) */

    /* cmap 子表 */
    int cmap_format;
    const unsigned char *cmap_data;
    unsigned int cmap_len;

    /* format 4 字段 */
    unsigned short seg_count_x2;
    const unsigned char *f4_end;
    const unsigned char *f4_start;
    const unsigned char *f4_delta;
    const unsigned char *f4_range;

    /* format 12 字段 */
    unsigned int f12_num_groups;
    const unsigned char *f12_groups;

    /* hmtx 表 */
    const unsigned char *hmtx_data;
    unsigned int hmtx_len;

    /* loca + glyf 表 */
    const unsigned char *loca_data;
    const unsigned char *glyf_data;
    unsigned int glyf_len;
};

/* ==================== 查找表 ==================== */
static const struct ttf_table_loc *find_table(struct ttf_font *f, const char *tag) {
    for (int i = 0; i < f->num_tables; i++) {
        int match = 1;
        for (int j = 0; j < 4; j++) {
            if (f->tables[i].tag[j] != tag[j]) { match = 0; break; }
        }
        if (match) return &f->tables[i];
    }
    return 0;
}

/* ==================== 解析 TTF 头部 ==================== */
static int parse_header(struct ttf_font *f) {
    /* SFNT 版本: 0x00010000 (TTF) 或 'OTTO' (OTF) */
    unsigned int sfnt = rd_u32(f->data);
    if (sfnt != 0x00010000u && sfnt != 0x74727565u /* 'true' */) {
        TTF_LOG("[TTF] not a TrueType font\n");
        return -1;
    }
    unsigned short num_tables = rd_u16(f->data + 4);
    if (num_tables > TTF_MAX_TABLES) num_tables = TTF_MAX_TABLES;

    f->num_tables = num_tables;
    const unsigned char *dir = f->data + 12;
    for (int i = 0; i < num_tables; i++) {
        const unsigned char *e = dir + i * 16;
        for (int j = 0; j < 4; j++) f->tables[i].tag[j] = e[j];
        f->tables[i].tag[4] = 0;
        f->tables[i].offset = rd_u32(e + 8);
        f->tables[i].length = rd_u32(e + 12);
    }
    return 0;
}

/* ==================== 解析 head 表 ==================== */
static int parse_head(struct ttf_font *f) {
    const struct ttf_table_loc *t = find_table(f, "head");
    if (!t) return -1;
    const unsigned char *p = f->data + t->offset;
    f->units_per_em = rd_u16(p + 18);
    f->loca_format = rd_i16(p + 50);
    return 0;
}

/* ==================== 解析 maxp 表 ==================== */
static int parse_maxp(struct ttf_font *f) {
    const struct ttf_table_loc *t = find_table(f, "maxp");
    if (!t) return -1;
    f->num_glyphs = rd_u16(f->data + t->offset + 4);
    return 0;
}

/* ==================== 解析 hhea 表 ==================== */
static int parse_hhea(struct ttf_font *f) {
    const struct ttf_table_loc *t = find_table(f, "hhea");
    if (!t) return -1;
    /* hhea 表布局 (OpenType 规范):
     *   offset 0: version (Fixed, 4B)
     *   offset 4: ascent (int16)
     *   offset 6: descent (int16, 负值)
     *   offset 8: lineGap (int16)
     *   offset 34: numberOfHMetrics (uint16) */
    f->ascent  = rd_i16(f->data + t->offset + 4);
    f->descent = rd_i16(f->data + t->offset + 6);
    f->num_hmetrics = rd_u16(f->data + t->offset + 34);
    return 0;
}

/* ==================== 解析 cmap 表 ==================== */
static int parse_cmap(struct ttf_font *f) {
    const struct ttf_table_loc *t = find_table(f, "cmap");
    if (!t) return -1;
    const unsigned char *cmap = f->data + t->offset;
    unsigned short num_sub = rd_u16(cmap + 2);

    /* 优先选择 format 12 (全 Unicode), 其次 format 4 (BMP) */
    const unsigned char *best4 = 0, *best12 = 0;
    unsigned int best4_len = 0, best12_len = 0;

    for (int i = 0; i < num_sub; i++) {
        const unsigned char *e = cmap + 4 + i * 8;
        unsigned short plat = rd_u16(e);
        unsigned short enc  = rd_u16(e + 2);
        unsigned int off = rd_u32(e + 4);
        const unsigned char *sub = cmap + off;
        unsigned short fmt = rd_u16(sub);

        /* 平台 3 (Windows) 或 0 (Unicode) */
        if (plat == 3 || plat == 0) {
            if (fmt == 12 && !best12) {
                best12 = sub;
                best12_len = t->length - off;
            }
            if (fmt == 4 && !best4) {
                best4 = sub;
                best4_len = t->length - off;
            }
        }
    }

    if (best12) {
        f->cmap_format = 12;
        f->cmap_data = best12;
        f->cmap_len = best12_len;
        f->f12_num_groups = rd_u32(best12 + 12);
        f->f12_groups = best12 + 16;
    } else if (best4) {
        f->cmap_format = 4;
        f->cmap_data = best4;
        f->cmap_len = best4_len;
        f->seg_count_x2 = rd_u16(best4 + 6);
        f->f4_end   = best4 + 14;
        f->f4_start = f->f4_end + f->seg_count_x2 + 2;  /* +2 for reservedPad */
        f->f4_delta = f->f4_start + f->seg_count_x2;
        f->f4_range = f->f4_delta + f->seg_count_x2;
    } else {
        TTF_LOG("[TTF] no usable cmap subtable\n");
        return -1;
    }
    return 0;
}

/* ==================== Unicode → 字形索引 ==================== */
static unsigned int char_to_glyph(struct ttf_font *f, unsigned int cp) {
    if (f->cmap_format == 12) {
        /* format 12: 分组二分查找 */
        unsigned int lo = 0, hi = f->f12_num_groups;
        while (lo < hi) {
            unsigned int mid = (lo + hi) / 2;
            const unsigned char *g = f->f12_groups + mid * 12;
            unsigned int start = rd_u32(g);
            unsigned int end   = rd_u32(g + 4);
            if (cp < start) hi = mid;
            else if (cp > end) lo = mid + 1;
            else {
                unsigned int glyph_id = rd_u32(g + 8);
                return glyph_id + (cp - start);
            }
        }
        return 0;
    } else if (f->cmap_format == 4) {
        /* format 4: 段映射 */
        unsigned short seg_count = f->seg_count_x2 / 2;
        for (unsigned int i = 0; i < seg_count; i++) {
            unsigned short end   = rd_u16(f->f4_end + i * 2);
            unsigned short start = rd_u16(f->f4_start + i * 2);
            if (cp >= start && cp <= end) {
                short delta = rd_i16(f->f4_delta + i * 2);
                unsigned short range_off = rd_u16(f->f4_range + i * 2);
                if (range_off == 0) {
                    return (unsigned short)((int)cp + delta);
                } else {
                    const unsigned char *base = f->f4_range + i * 2 + range_off;
                    unsigned short gid = rd_u16(base + (cp - start) * 2);
                    if (gid == 0) return 0;
                    return (unsigned short)((int)gid + delta);
                }
            }
            if (end == 0xFFFF) break;
        }
        return 0;
    }
    return 0;
}

/* ==================== 获取字形 advance width ==================== */
static int get_advance_width(struct ttf_font *f, unsigned int glyph_id) {
    if (glyph_id >= f->num_glyphs) return 0;
    if (glyph_id >= f->num_hmetrics) glyph_id = f->num_hmetrics - 1;
    /* hMetrics: 每条目 4 字节 (advanceWidth u16 + lsb i16) */
    return rd_u16(f->hmtx_data + glyph_id * 4);
}

/* ==================== 获取字形偏移 (loca 表) ==================== */
static unsigned int get_glyph_offset(struct ttf_font *f, unsigned int glyph_id) {
    if (glyph_id >= f->num_glyphs) return 0;
    if (f->loca_format == 0) {
        /* short format: offset = u16 * 2 */
        return rd_u16(f->loca_data + glyph_id * 2) * 2u;
    } else {
        /* long format: offset = u32 */
        return rd_u32(f->loca_data + glyph_id * 4);
    }
}

static unsigned int get_glyph_length(struct ttf_font *f, unsigned int glyph_id) {
    unsigned int start = get_glyph_offset(f, glyph_id);
    unsigned int end   = get_glyph_offset(f, glyph_id + 1);
    if (end < start) return 0;
    return end - start;
}

/* ==================== 字形点提取 ==================== */
#define TTF_MAX_POINTS 4096
#define TTF_MAX_CONTOURS 64

struct ttf_point {
    int x, y;           /* 字体单位坐标 */
    int on_curve;       /* 1=在线上, 0=控制点 */
};

struct ttf_outline {
    int num_contours;
    int contour_start[TTF_MAX_CONTOURS];  /* 每条轮廓的起始点索引 */
    int contour_end[TTF_MAX_CONTOURS];    /* 每条轮廓的结束点索引 (含) */
    struct ttf_point points[TTF_MAX_POINTS];
    int num_points;
    int x_min, y_min, x_max, y_max;
};

/* 解析简单字形的点数据 */
static int parse_simple_glyph(struct ttf_font *f, const unsigned char *p,
                              unsigned int glyph_len, struct ttf_outline *out) {
    if (glyph_len < 12) return -1;
    int num_contours = rd_i16(p);
    if (num_contours <= 0) return -1;
    if (num_contours > TTF_MAX_CONTOURS) return -1;

    out->num_contours = num_contours;
    out->x_min = rd_i16(p + 2);
    out->y_min = rd_i16(p + 4);
    out->x_max = rd_i16(p + 6);
    out->y_max = rd_i16(p + 8);

    const unsigned char *q = p + 10;
    /* endPtsOfContours */
    int last = 0;
    for (int i = 0; i < num_contours; i++) {
        unsigned short ep = rd_u16(q);
        q += 2;
        out->contour_start[i] = (i == 0) ? 0 : last + 1;
        out->contour_end[i] = ep;
        last = ep;
    }
    int total_pts = last + 1;
    if (total_pts > TTF_MAX_POINTS) return -1;
    out->num_points = total_pts;

    /* instructionLength */
    unsigned short inst_len = rd_u16(q);
    q += 2 + inst_len;

    /* flags (带 repeat 压缩) */
    static unsigned char flags[TTF_MAX_POINTS];
    int fi = 0;
    while (fi < total_pts) {
        unsigned char fl = *q++;
        flags[fi++] = fl;
        if (fl & 0x08) {  /* repeat */
            unsigned char cnt = *q++;
            while (cnt-- && fi < total_pts) flags[fi++] = fl;
        }
    }

    /* X 坐标 (delta 编码) */
    int x = 0;
    for (int i = 0; i < total_pts; i++) {
        unsigned char fl = flags[i];
        if (fl & 0x02) {  /* x-short */
            int dx = *q++;
            if (!(fl & 0x10)) dx = -dx;  /* x-same-or-positive */
            x += dx;
        } else {
            if (fl & 0x10) {  /* x-same */
                /* x 不变 */
            } else {
                x += rd_i16(q); q += 2;
            }
        }
        out->points[i].x = x;
    }

    /* Y 坐标 (delta 编码) */
    int y = 0;
    for (int i = 0; i < total_pts; i++) {
        unsigned char fl = flags[i];
        if (fl & 0x04) {  /* y-short */
            int dy = *q++;
            if (!(fl & 0x20)) dy = -dy;
            y += dy;
        } else {
            if (fl & 0x20) {
                /* y 不变 */
            } else {
                y += rd_i16(q); q += 2;
            }
        }
        out->points[i].y = y;
        out->points[i].on_curve = (fl & 0x01) ? 1 : 0;
    }

    return 0;
}

/* 解析复合字形 (仅支持平移, 不支持缩放/旋转) */
static int parse_composite_glyph(struct ttf_font *f, const unsigned char *p,
                                 unsigned int glyph_len, struct ttf_outline *out,
                                 int depth) {
    if (depth > 4) return -1;  /* 防止递归过深 */
    if (glyph_len < 12) return -1;

    out->num_contours = 0;
    out->num_points = 0;
    out->x_min = 0; out->y_min = 0; out->x_max = 0; out->y_max = 0;

    const unsigned char *q = p + 10;  /* skip numberOfContours + bbox */

    for (;;) {
        if (q + 4 > p + glyph_len) break;
        unsigned short flags = rd_u16(q); q += 2;
        unsigned short glyph_id = rd_u16(q); q += 2;

        int dx = 0, dy = 0;
        if (flags & 0x0001) {  /* ARGS_ARE_WORDS */
            dx = rd_i16(q); q += 2;
            dy = rd_i16(q); q += 2;
        } else {
            dx = (signed char)*q++;
            dy = (signed char)*q++;
        }

        if (!(flags & 0x0002)) {  /* not ARGS_ARE_XY_VALUES: 点匹配, 暂不支持 */
            dx = 0; dy = 0;
        }

        /* 跳过变换矩阵 */
        if (flags & 0x0008) { q += 2; }       /* WE_HAVE_A_SCALE */
        if (flags & 0x0040) { q += 4; }       /* WE_HAVE_AN_X_AND_Y_SCALE */
        if (flags & 0x0080) { q += 8; }       /* WE_HAVE_A_TWO_BY_TWO */

        /* 递归解析子字形 */
        unsigned int sub_off = get_glyph_offset(f, glyph_id);
        unsigned int sub_len = get_glyph_length(f, glyph_id);
        if (sub_len == 0) goto next_component;

        const unsigned char *sub_p = f->glyf_data + sub_off;
        int sub_nc = rd_i16(sub_p);

        if (sub_nc >= 0) {
            /* 简单子字形 */
            struct ttf_outline sub;
            if (parse_simple_glyph(f, sub_p, sub_len, &sub) == 0) {
                int base = out->num_points;
                for (int i = 0; i < sub.num_points && out->num_points < TTF_MAX_POINTS; i++) {
                    out->points[out->num_points].x = sub.points[i].x + dx;
                    out->points[out->num_points].y = sub.points[i].y + dy;
                    out->points[out->num_points].on_curve = sub.points[i].on_curve;
                    out->num_points++;
                }
                for (int i = 0; i < sub.num_contours && out->num_contours < TTF_MAX_CONTOURS; i++) {
                    out->contour_start[out->num_contours] = base + sub.contour_start[i];
                    out->contour_end[out->num_contours] = base + sub.contour_end[i];
                    out->num_contours++;
                }
                /* 合并 bbox: 取所有组件的并集 (union).
                 * [修复] 旧代码只在 out->num_contours == sub.num_contours 时设置 bbox,
                 * 即只保留第一个组件的 bbox。对于 "i" 这样的复合字形 (stem + dot),
                 * dot 组件的 y_max > stem 的 y_max, 但 bbox 没有更新 →
                 * 渲染时位图顶部不包含 dot 区域 → dot 被裁掉, "i" 没有上面的点。
                 * 正确做法: 对每个组件都做 bbox union。 */
                if (out->num_contours == sub.num_contours) {
                    /* 第一个组件: 直接赋值 */
                    out->x_min = sub.x_min + dx; out->y_min = sub.y_min + dy;
                    out->x_max = sub.x_max + dx; out->y_max = sub.y_max + dy;
                } else {
                    /* 后续组件: 取并集 */
                    if (sub.x_min + dx < out->x_min) out->x_min = sub.x_min + dx;
                    if (sub.y_min + dy < out->y_min) out->y_min = sub.y_min + dy;
                    if (sub.x_max + dx > out->x_max) out->x_max = sub.x_max + dx;
                    if (sub.y_max + dy > out->y_max) out->y_max = sub.y_max + dy;
                }
            }
        } else if (depth < 4) {
            /* 递归复合字形 */
            struct ttf_outline sub;
            if (parse_composite_glyph(f, sub_p, sub_len, &sub, depth + 1) == 0) {
                int base = out->num_points;
                for (int i = 0; i < sub.num_points && out->num_points < TTF_MAX_POINTS; i++) {
                    out->points[out->num_points].x = sub.points[i].x + dx;
                    out->points[out->num_points].y = sub.points[i].y + dy;
                    out->points[out->num_points].on_curve = sub.points[i].on_curve;
                    out->num_points++;
                }
                for (int i = 0; i < sub.num_contours && out->num_contours < TTF_MAX_CONTOURS; i++) {
                    out->contour_start[out->num_contours] = base + sub.contour_start[i];
                    out->contour_end[out->num_contours] = base + sub.contour_end[i];
                    out->num_contours++;
                }
                /* [同上修复] 递归复合字形也做 bbox union */
                if (out->num_contours == sub.num_contours) {
                    out->x_min = sub.x_min + dx; out->y_min = sub.y_min + dy;
                    out->x_max = sub.x_max + dx; out->y_max = sub.y_max + dy;
                } else {
                    if (sub.x_min + dx < out->x_min) out->x_min = sub.x_min + dx;
                    if (sub.y_min + dy < out->y_min) out->y_min = sub.y_min + dy;
                    if (sub.x_max + dx > out->x_max) out->x_max = sub.x_max + dx;
                    if (sub.y_max + dy > out->y_max) out->y_max = sub.y_max + dy;
                }
            }
        }

    next_component:
        if (!(flags & 0x0020)) break;  /* no MORE_COMPONENTS */
    }
    return 0;
}

/* ==================== 获取字形轮廓 ==================== */
static int get_glyph_outline(struct ttf_font *f, unsigned int glyph_id,
                             struct ttf_outline *out) {
    out->num_contours = 0;
    out->num_points = 0;

    if (glyph_id == 0 || glyph_id >= f->num_glyphs) return -1;

    unsigned int goff = get_glyph_offset(f, glyph_id);
    unsigned int glen = get_glyph_length(f, glyph_id);
    if (glen == 0) return -1;

    const unsigned char *p = f->glyf_data + goff;
    int num_contours = rd_i16(p);

    if (num_contours >= 0) {
        return parse_simple_glyph(f, p, glen, out);
    } else {
        return parse_composite_glyph(f, p, glen, out, 0);
    }
}

/* ==================== 贝塞尔展平 → 线段 ==================== */
#define TTF_MAX_EDGES 8192

struct ttf_edge {
    int x1, y1, x2, y2;  /* 像素坐标 (已缩放) */
};

/* 递归展平二次贝塞尔曲线 */
static void flatten_quad(int x0, int y0, int x1, int y1, int x2, int y2,
                         struct ttf_edge *edges, int *num_edges, int max_edges) {
    /* 平坦度测试: |P0 - 2*P1 + P2| < 阈值 */
    int dx = x0 - 2*x1 + x2;
    int dy = y0 - 2*y1 + y2;
    int flatness = dx*dx + dy*dy;
    if (flatness < 4 || *num_edges >= max_edges - 1) {
        /* 足够平坦, 输出直线 */
        if (*num_edges < max_edges) {
            edges[*num_edges].x1 = x0; edges[*num_edges].y1 = y0;
            edges[*num_edges].x2 = x2; edges[*num_edges].y2 = y2;
            (*num_edges)++;
        }
        return;
    }
    /* 中点细分 */
    int mx01 = (x0 + x1) / 2, my01 = (y0 + y1) / 2;
    int mx12 = (x1 + x2) / 2, my12 = (y1 + y2) / 2;
    int mx = (mx01 + mx12) / 2, my = (my01 + my12) / 2;
    flatten_quad(x0, y0, mx01, my01, mx, my, edges, num_edges, max_edges);
    flatten_quad(mx, my, mx12, my12, x2, y2, edges, num_edges, max_edges);
}

/* 将轮廓转换为边 (线段) */
static int outline_to_edges(struct ttf_outline *out, int scale, int shift,
                            struct ttf_edge *edges, int max_edges) {
    int num_edges = 0;

    for (int c = 0; c < out->num_contours; c++) {
        int start = out->contour_start[c];
        int end = out->contour_end[c];
        int n = end - start + 1;
        if (n < 1) continue;

        /* 缩放坐标: pixel = font_unit * scale >> shift */
        #define SCALE(v) ((v * scale) >> shift)

        /* 找到第一个 on-curve 点作为起点 */
        int first_on = -1;
        for (int i = 0; i < n; i++) {
            if (out->points[start + i].on_curve) { first_on = i; break; }
        }

        int prev_x, prev_y;
        if (first_on >= 0) {
            prev_x = SCALE(out->points[start + first_on].x);
            prev_y = SCALE(out->points[start + first_on].y);
        } else {
            /* 全部是 off-curve: 用第一个点和第二个点的中点作为隐式 on-curve */
            int mx = (out->points[start].x + out->points[start + 1].x) / 2;
            int my = (out->points[start].y + out->points[start + 1].y) / 2;
            prev_x = SCALE(mx);
            prev_y = SCALE(my);
            first_on = n;  /* 标记无显式 on-curve 起点 */
        }

        /* 遍历轮廓点 */
        for (int i = 1; i <= n; i++) {
            int idx = (first_on >= 0 ? first_on : 0) + i;
            if (first_on >= 0) idx = first_on + i;
            else idx = i;
            idx = start + (idx % n);

            struct ttf_point *pt = &out->points[idx];
            int cx = SCALE(pt->x);
            int cy = SCALE(pt->y);

            if (pt->on_curve) {
                /* 直线: prev → current */
                if (num_edges < max_edges) {
                    edges[num_edges].x1 = prev_x; edges[num_edges].y1 = prev_y;
                    edges[num_edges].x2 = cx; edges[num_edges].y2 = cy;
                    num_edges++;
                }
                prev_x = cx; prev_y = cy;
            } else {
                /* 控制点: 找下一个 on-curve 点 */
                int next_idx = start + ((idx - start + 1) % n);
                struct ttf_point *next_pt = &out->points[next_idx];
                int nx, ny;
                if (next_pt->on_curve) {
                    nx = SCALE(next_pt->x);
                    ny = SCALE(next_pt->y);
                } else {
                    /* 下一个也是 off-curve: 隐式 on-curve 中点 */
                    nx = SCALE((pt->x + next_pt->x) / 2);
                    ny = SCALE((pt->y + next_pt->y) / 2);
                }
                /* 贝塞尔: prev → control(pt) → next */
                flatten_quad(prev_x, prev_y, cx, cy, nx, ny,
                             edges, &num_edges, max_edges);
                prev_x = nx; prev_y = ny;
            }
        }
        #undef SCALE
    }

    return num_edges;
}

/* ==================== 扫描线栅格化器 (4x4 超采样) ==================== */
static struct ttf_bitmap *rasterize_glyph(struct ttf_font *f,
                                          unsigned int glyph_id, int pixel_size) {
    struct ttf_outline out;
    if (get_glyph_outline(f, glyph_id, &out) != 0) return 0;
    if (out.num_points == 0 || out.num_contours == 0) return 0;

    /* 缩放: font_unit → pixel, 使用定点数 (scale = pixel_size * SS_FACTOR / units_per_em)
     * SS_FACTOR = 4: 4x4 超采样, 产生 17 级灰度 (0,16,32,...,255), 边缘更细腻. */
    #define SS_FACTOR 4
    int scale_num = pixel_size * SS_FACTOR;
    int scale_den = f->units_per_em;
    if (scale_den == 0) return 0;

    /* 使用定点: shift = 20, scale = (scale_num << 20) / scale_den */
    int shift = 20;
    int scale = (int)(((long long)scale_num << shift) / scale_den);

    /* 边 */
    static struct ttf_edge edges[TTF_MAX_EDGES];
    /* [DDDDDD 防漏 · 防御清零] edges / crossings / ss_buf 都是 static,
     * 上一个字形 (比如 'D') 的数据可能残留; 如果本字形 (SPACE 等空字形)
     * 在 outline_to_edges / scanline 某路径提前 return, 可能不会重新写满
     * 这些静态区, 但如果某段逻辑读取了 "本应重写但没重写的字段",
     * 会把之前 'D' 的 edges 当成当前字形的 edges 渲染出来 → 6 个 D。
     * 这里每次栅格化前把它们清零 (edges/crossings 零成本 memset, ss_buf 512*512 用多少清多少),
     * 保证即使中间提前 return, 也不会读到上一个字形残留。 */
    /* edges: 清全表 (8192 条, 每条 4 个 int = 16 字节)。
     * 不用 memset (freestanding 驱动下 libc 不一定可用), 直接遍历。 */
    for (int ei = 0; ei < TTF_MAX_EDGES; ei++) {
        edges[ei].x1 = edges[ei].x2 = edges[ei].y1 = edges[ei].y2 = 0;
    }
    int num_edges = outline_to_edges(&out, scale, shift, edges, TTF_MAX_EDGES);
    if (num_edges == 0) return 0;

    /* 计算位图边界 */
    int x_min = out.x_min, x_max = out.x_max;
    int y_min = out.y_min, y_max = out.y_max;
    /* 缩放到像素 (SS_FACTOR x) */
    int px_min = (int)(((long long)x_min * scale_num) / scale_den) - SS_FACTOR;
    int py_min = (int)(((long long)y_min * scale_num) / scale_den) - SS_FACTOR;
    int px_max = (int)(((long long)x_max * scale_num) / scale_den) + SS_FACTOR;
    int py_max = (int)(((long long)y_max * scale_num) / scale_den) + SS_FACTOR;
    if (px_min < 0) px_min = 0;
    /* [descender 修复] py_min < 0 是正常的 (descender 在基线以下, TTF y 坐标为负).
     * 之前 if (py_min < 0) py_min = 0 会截断 descender, 导致 g/p/y/q/j 等字符
     * 下半部分被切掉. 保留负值让位图包含完整 descender 区域. */
    if (py_min < -SS_FACTOR * 4) py_min = -SS_FACTOR * 4;  /* 防止极端值, 允许 ~4px descender */

    int bmp_w = px_max - px_min;  /* SS_FACTOR x 分辨率 */
    int bmp_h = py_max - py_min;
    if (bmp_w <= 0 || bmp_h <= 0) return 0;
    /* 限制超采样缓冲区尺寸: 4x 分辨率下, 64px 字形 = 256 ss 像素 */
    if (bmp_w > 512) bmp_w = 512;
    if (bmp_h > 512) bmp_h = 512;

    /* 分配 SS_FACTOR x 分辨率位图 (1-bit: 0 或 1) */
    int ss_w = bmp_w;
    int ss_h = bmp_h;
    static unsigned char ss_buf[512 * 512];  /* 4x 超采样缓冲区 */
    for (int i = 0; i < ss_w * ss_h; i++) ss_buf[i] = 0;

    /* 边偏移: x 方向偏移到 (0,0) 起始 (y 方向不偏移, 用绝对坐标比较) */
    int off_x = px_min;

    /* 扫描线填充 (even-odd 规则) */
    /* [关键] TTF 字体 y 轴向上 (y_max=字形顶部, y_min=字形底部),
     *        但屏幕位图 y 轴向下 (row 0 = 顶部).
     *        因此扫描时翻转 y 轴: row 0 对应 font y_max (顶部),
     *        末行对应 font y_min (底部). 边坐标全部用绝对 font 坐标比较,
     *        避免 absolute/relative 混用导致的偏移错位. */
    static int crossings[512];
    for (int y = 0; y < ss_h; y++) {
        int y_coord = py_max - y;  /* 翻转: row 0 = 字形顶部 (font y_max) */
        int ncross = 0;

        for (int e = 0; e < num_edges; e++) {
            int ey1 = edges[e].y1;  /* 绝对 font 坐标 (向上为正) */
            int ey2 = edges[e].y2;
            if (ey1 == ey2) continue;  /* 水平边跳过 */

            int y_lo = (ey1 < ey2) ? ey1 : ey2;
            int y_hi = (ey1 < ey2) ? ey2 : ey1;
            if (y_coord < y_lo || y_coord >= y_hi) continue;

            /* 计算 x 交点: x = x1 + (y - y1) * (x2 - x1) / (y2 - y1) */
            int dy = ey2 - ey1;
            int dx = edges[e].x2 - edges[e].x1;
            int x_cross = edges[e].x1 - off_x +
                          (int)(((long long)(y_coord - ey1) * dx) / dy);
            if (ncross < 512) crossings[ncross++] = x_cross;
        }

        /* 排序交点 (插入排序) */
        for (int i = 1; i < ncross; i++) {
            int key = crossings[i];
            int j = i - 1;
            while (j >= 0 && crossings[j] > key) {
                crossings[j+1] = crossings[j];
                j--;
            }
            crossings[j+1] = key;
        }

        /* 成对填充 */
        for (int i = 0; i + 1 < ncross; i += 2) {
            int x1 = crossings[i];
            int x2 = crossings[i+1];
            if (x1 < 0) x1 = 0;
            if (x2 > ss_w) x2 = ss_w;
            for (int x = x1; x < x2; x++) {
                ss_buf[y * ss_w + x] = 1;
            }
        }
    }

    /* 降采样: SS_FACTOR x SS_FACTOR → 1x, 计算 alpha (0-255)
     * 4x4 = 16 个子样本, alpha = cnt * 255 / 16 (17 级灰度) */
    int out_w = (ss_w + SS_FACTOR - 1) / SS_FACTOR;
    int out_h = (ss_h + SS_FACTOR - 1) / SS_FACTOR;

    struct ttf_bitmap *bmp = (struct ttf_bitmap *)s_malloc(sizeof(struct ttf_bitmap));
    if (!bmp) return 0;
    bmp->width = out_w;
    bmp->height = out_h;
    bmp->data = (unsigned char *)s_malloc(out_w * out_h);
    if (!bmp->data) { s_free(bmp); return 0; }

    int ss_total = SS_FACTOR * SS_FACTOR;  /* 16 */
    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            int cnt = 0;
            int sx0 = x * SS_FACTOR, sy0 = y * SS_FACTOR;
            for (int dy = 0; dy < SS_FACTOR; dy++) {
                for (int dx = 0; dx < SS_FACTOR; dx++) {
                    int sx = sx0 + dx, sy = sy0 + dy;
                    if (sx < ss_w && sy < ss_h && ss_buf[sy * ss_w + sx]) cnt++;
                }
            }
            /* 0..16 → 0..255 (线性映射, 17 级灰度) */
            bmp->data[y * out_w + x] = (unsigned char)((cnt * 255 + ss_total/2) / ss_total);
        }
    }

    /* 计算 advance width 和 bearing (像素) */
    int adv_font = get_advance_width(f, glyph_id);
    bmp->advance = (int)(((long long)adv_font * pixel_size) / scale_den);
    bmp->bearing_x = (int)(((long long)x_min * pixel_size) / scale_den);
    /* bearing_y: 从基线到字形顶部 (在像素坐标中, y 向上为正在字体坐标中,
     * 但在屏幕坐标中 y 向下为正, 所以 bearing_y = y_max 对应的像素值) */
    bmp->bearing_y = (int)(((long long)y_max * pixel_size) / scale_den);

    return bmp;
}

/* ==================== 字形缓存 ==================== */
#define TTF_CACHE_SIZE 512

struct cache_entry {
    unsigned int codepoint;
    int pixel_size;
    struct ttf_bitmap *bmp;
    int tick;
};

static struct cache_entry g_cache[TTF_CACHE_SIZE];
static int g_cache_tick = 0;

static struct ttf_bitmap *cache_lookup(unsigned int cp, int ps) {
    for (int i = 0; i < TTF_CACHE_SIZE; i++) {
        if (g_cache[i].bmp && g_cache[i].codepoint == cp && g_cache[i].pixel_size == ps) {
            g_cache[i].tick = ++g_cache_tick;
            return g_cache[i].bmp;
        }
    }
    return 0;
}

static void cache_insert(unsigned int cp, int ps, struct ttf_bitmap *bmp) {
    /* 找空槽或最旧的条目 */
    int victim = 0;
    int min_tick = g_cache[0].tick;
    for (int i = 0; i < TTF_CACHE_SIZE; i++) {
        if (!g_cache[i].bmp) { victim = i; break; }
        if (g_cache[i].tick < min_tick) {
            min_tick = g_cache[i].tick;
            victim = i;
        }
    }
    /* 释放旧条目 */
    if (g_cache[victim].bmp) {
        if (g_cache[victim].bmp->data) s_free(g_cache[victim].bmp->data);
        s_free(g_cache[victim].bmp);
    }
    g_cache[victim].codepoint = cp;
    g_cache[victim].pixel_size = ps;
    g_cache[victim].bmp = bmp;
    g_cache[victim].tick = ++g_cache_tick;
}

/* ==================== 公共 API ==================== */
struct ttf_font *ttf_load(const char *path) {
    if (!s_malloc || !s_read) return 0;

    /* 先读取文件大小: 用一个探测读取 */
    /* 分配大缓冲区 (字体最大 16MB) */
    int buf_size = 16 * 1024 * 1024;
    unsigned char *buf = (unsigned char *)s_malloc(buf_size);
    if (!buf) {
        TTF_LOG("[TTF] malloc failed for font data\n");
        return 0;
    }

    int read = s_read(path, (char *)buf, buf_size);
    if (read <= 0) {
        TTF_LOG("[TTF] file_read failed: ");
        TTF_LOG(path);
        TTF_LOG("\n");
        s_free(buf);
        return 0;
    }

    struct ttf_font *f = (struct ttf_font *)s_malloc(sizeof(struct ttf_font));
    if (!f) { s_free(buf); return 0; }

    /* 清零 */
    unsigned char *fp = (unsigned char *)f;
    for (int i = 0; i < (int)sizeof(struct ttf_font); i++) fp[i] = 0;

    f->data = buf;
    f->data_size = read;

    /* 解析各表 */
    if (parse_header(f) != 0) { s_free(buf); s_free(f); return 0; }
    if (parse_head(f) != 0) { s_free(buf); s_free(f); return 0; }
    if (parse_maxp(f) != 0) { s_free(buf); s_free(f); return 0; }
    if (parse_hhea(f) != 0) { s_free(buf); s_free(f); return 0; }

    /* hmtx */
    const struct ttf_table_loc *ht = find_table(f, "hmtx");
    if (!ht) { s_free(buf); s_free(f); return 0; }
    f->hmtx_data = f->data + ht->offset;
    f->hmtx_len = ht->length;

    /* loca */
    const struct ttf_table_loc *lt = find_table(f, "loca");
    if (!lt) { s_free(buf); s_free(f); return 0; }
    f->loca_data = f->data + lt->offset;

    /* glyf */
    const struct ttf_table_loc *gt = find_table(f, "glyf");
    if (!gt) { s_free(buf); s_free(f); return 0; }
    f->glyf_data = f->data + gt->offset;
    f->glyf_len = gt->length;

    /* cmap */
    if (parse_cmap(f) != 0) { s_free(buf); s_free(f); return 0; }

    /* 清空缓存 */
    for (int i = 0; i < TTF_CACHE_SIZE; i++) g_cache[i].bmp = 0;

    TTF_LOG("[TTF] loaded: glyphs=");
    /* 输出 num_glyphs 到串口 (简化) */
    {
        char numbuf[16]; int n = 0;
        unsigned short ng = f->num_glyphs;
        if (ng == 0) { numbuf[n++] = '0'; }
        else {
            char tmp[8]; int t = 0;
            while (ng) { tmp[t++] = '0' + (ng % 10); ng /= 10; }
            while (t--) numbuf[n++] = tmp[t];
        }
        numbuf[n] = 0;
        if (s_log) {
            s_log("[TTF] ");
            s_log(numbuf);
            s_log(" glyphs, cmap format ");
            char fmt[8];
            fmt[0] = '0' + f->cmap_format;
            fmt[1] = '\n';
            fmt[2] = 0;
            s_log(fmt);
        }
    }

    return f;
}

struct ttf_bitmap *ttf_get_bitmap(struct ttf_font *f, unsigned int codepoint,
                                  int pixel_size) {
    if (!f) return 0;

    /* 缓存查找 */
    struct ttf_bitmap *cached = cache_lookup(codepoint, pixel_size);
    if (cached) return cached;

    /* Unicode → 字形索引 */
    unsigned int gid = char_to_glyph(f, codepoint);
    if (gid == 0) return 0;

    /* 栅格化 */
    struct ttf_bitmap *bmp = rasterize_glyph(f, gid, pixel_size);
    if (!bmp) return 0;

    /* 存入缓存 */
    cache_insert(codepoint, pixel_size, bmp);
    return bmp;
}

/* 获取字体 ascent (像素): 基线距 cell 顶部的距离.
 * 用于在字符单元中正确定位基线, 避免文字偏移或 descender 被裁剪. */
int ttf_get_ascent_px(struct ttf_font *f, int pixel_size) {
    if (!f || f->units_per_em == 0) return pixel_size;
    return (int)(((long long)f->ascent * pixel_size) / f->units_per_em);
}

/* 获取字符推进宽度 (像素): 等宽字体校准用.
 * 返回该字符在指定 pixel_size 下的 advance 宽度 (四舍五入). */
int ttf_get_advance_px(struct ttf_font *f, unsigned int codepoint, int pixel_size) {
    if (!f || f->units_per_em == 0) return pixel_size;
    unsigned int gid = char_to_glyph(f, codepoint);
    int adv_font = get_advance_width(f, gid);
    return (int)(((long long)adv_font * pixel_size + f->units_per_em / 2) / f->units_per_em);
}

void ttf_free(struct ttf_font *f) {
    if (!f) return;
    /* 释放缓存 */
    for (int i = 0; i < TTF_CACHE_SIZE; i++) {
        if (g_cache[i].bmp) {
            if (g_cache[i].bmp->data) s_free(g_cache[i].bmp->data);
            s_free(g_cache[i].bmp);
            g_cache[i].bmp = 0;
        }
    }
    if (f->data) s_free(f->data);
    s_free(f);
}
