#include "broadcast.h"
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "ui.h"

static const char *TAG = "LRC";

// 全局歌词管理器，封装所有 UI 状态
static lrc_ui_manager_t s_lrc_mgr = {NULL, NULL, -1, {}};

/**
 * @brief 解析单行 LRC 时间戳
 */
static int parse_lrc_time(const std::string& timeStr) {
    int min = 0, sec = 0, ms = 0;
    // 支持 [mm:ss.xx] 或 [mm:ss:xx]
    if (sscanf(timeStr.c_str(), "%d:%d.%d", &min, &sec, &ms) == 3 ||
        sscanf(timeStr.c_str(), "%d:%d:%d", &min, &sec, &ms) == 3) {
        // 百分秒转毫秒
        if (ms < 100 && timeStr.find('.') != std::string::npos) {
            ms *= 10;
        }
        return min * 60000 + sec * 1000 + ms;
    }
    return -1;
}

/**
 * @brief 去除字符串两端空格
 */
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::vector<LrcLine> loadLrcFile(const std::string& filePath) {
    std::vector<LrcLine> list;
    FILE* file = fopen(filePath.c_str(), "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open: %s", filePath.c_str());
        return list;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), file)) {
        std::string lineStr(buf);
        size_t lPos = lineStr.find('[');
        size_t rPos = lineStr.find(']');
        if (lPos == std::string::npos || rPos == std::string::npos) continue;

        int timeMs = parse_lrc_time(lineStr.substr(lPos + 1, rPos - lPos - 1));
        if (timeMs < 0) continue;

        std::string lyric = (rPos + 1 < lineStr.size()) ? trim(lineStr.substr(rPos + 1)) : "";
        if (!lyric.empty()) {
            list.push_back({timeMs, lyric, NULL});
        }
    }
    fclose(file);
    return list;
}

std::string getCurrentLyric(int currentMS, const std::vector<LrcLine>& list) {
    if (list.empty()) return "";
    int index = -1;
    for (int i = 0; i < (int)list.size(); ++i) {
        if (currentMS >= list[i].timeMs) index = i;
        else break;
    }
    return (index != -1) ? list[index].lyric : "";
}

void initMorningAnimationLyrics(const std::string& filePath) {
    // 1. 加载数据
    s_lrc_mgr.lines = loadLrcFile(filePath);
    if (s_lrc_mgr.lines.empty()) return;

    // 2. 环境清理
    if (ui_MorningAnimationLabel) lv_obj_add_flag(ui_MorningAnimationLabel, LV_OBJ_FLAG_HIDDEN);
    if (s_lrc_mgr.viewport) lv_obj_del(s_lrc_mgr.viewport);

    // 3. 创建视口 (裁剪区域)
    s_lrc_mgr.viewport = lv_obj_create(ui_MorningAnimationContainer);
    lv_obj_remove_style_all(s_lrc_mgr.viewport);
    lv_obj_set_size(s_lrc_mgr.viewport, 280, 150);
    lv_obj_set_align(s_lrc_mgr.viewport, LV_ALIGN_CENTER);
    lv_obj_set_y(s_lrc_mgr.viewport, 45); // 避开顶部图片
    lv_obj_set_style_clip_corner(s_lrc_mgr.viewport, true, 0);

    // 4. 创建滚动容器
    s_lrc_mgr.scroller = lv_obj_create(s_lrc_mgr.viewport);
    lv_obj_remove_style_all(s_lrc_mgr.scroller);
    lv_obj_set_width(s_lrc_mgr.scroller, lv_pct(100));
    lv_obj_set_height(s_lrc_mgr.scroller, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_lrc_mgr.scroller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_lrc_mgr.scroller, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_lrc_mgr.scroller, 12, 0);

    // 5. 生成歌词 Label
    for (auto& line : s_lrc_mgr.lines) {
        line.label = lv_label_create(s_lrc_mgr.scroller);
        lv_label_set_text(line.label, line.lyric.c_str());
        lv_obj_set_width(line.label, lv_pct(100));
        lv_label_set_long_mode(line.label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(line.label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(line.label, &ui_font_sfprodisplay18, 0);
        lv_obj_set_style_text_color(line.label, lv_color_hex(0x606060), 0); // 未激活颜色
    }

    s_lrc_mgr.current_index = -1;
    lv_obj_update_layout(s_lrc_mgr.scroller);
    updateMorningAnimationLyrics(0);
}

void updateMorningAnimationLyrics(int currentMS) {
    if (s_lrc_mgr.lines.empty() || !s_lrc_mgr.scroller) return;

    // 高频扫描：查找当前应该显示的行
    int searchTime = currentMS + 50; // 50ms 提前补偿
    int index = -1;

    // 优化：从当前索引开始查找，避免每次全量遍历
    int start_search = (s_lrc_mgr.current_index >= 0) ? s_lrc_mgr.current_index : 0;
    
    // 如果时间回退，则从头开始
    if (s_lrc_mgr.current_index >= 0 && searchTime < s_lrc_mgr.lines[s_lrc_mgr.current_index].timeMs) {
        start_search = 0;
    }

    for (int i = start_search; i < (int)s_lrc_mgr.lines.size(); ++i) {
        if (searchTime >= s_lrc_mgr.lines[i].timeMs) index = i;
        else break;
    }

    // 状态变更：更新高亮和滚动
    if (index != -1 && index != s_lrc_mgr.current_index) {
        // 恢复旧行
        if (s_lrc_mgr.current_index != -1) {
            lv_obj_set_style_text_color(s_lrc_mgr.lines[s_lrc_mgr.current_index].label, lv_color_hex(0x606060), 0);
        }
        
        // 高亮新行
        lv_obj_set_style_text_color(s_lrc_mgr.lines[index].label, lv_color_hex(0xFFFFFF), 0);

        // 计算居中偏移
        lv_obj_update_layout(s_lrc_mgr.scroller);
        int v_h = lv_obj_get_height(s_lrc_mgr.viewport);
        int l_y = lv_obj_get_y(s_lrc_mgr.lines[index].label);
        int l_h = lv_obj_get_height(s_lrc_mgr.lines[index].label);
        int target_y = (v_h / 2) - (l_y + l_h / 2);

        // 平滑滚动动画
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_lrc_mgr.scroller);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&a, lv_obj_get_y(s_lrc_mgr.scroller), target_y);
        lv_anim_set_duration(&a, 300);
        lv_anim_start(&a);

        s_lrc_mgr.current_index = index;
    }
}
