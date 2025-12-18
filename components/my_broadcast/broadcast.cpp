#include "broadcast.h"
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "ui.h"

static const char *TAG = "LRC";

std::vector<LrcLine> lrcList;
static lv_obj_t* s_lrc_scroller = NULL;
static int s_current_lrc_index = -1;

// 读取 lrc  拆成  时间 + 歌词
std::vector<LrcLine> loadLrcFile(const std::string& filePath)
{
    std::vector<LrcLine> list;

    ESP_LOGI(TAG, "Loading LRC file: %s", filePath.c_str());

    FILE* file = fopen(filePath.c_str(), "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open LRC file: %s", filePath.c_str());
        return list;   // 空
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // 去掉换行符
        line[strcspn(line, "\r\n")] = 0;

        if (strlen(line) == 0) continue;

        std::string lineStr(line);
        size_t lPos = lineStr.find('[');
        size_t rPos = lineStr.find(']');

        if (lPos == std::string::npos || rPos == std::string::npos) {
            continue;  
        }

        std::string timeStr = lineStr.substr(lPos + 1, rPos - lPos - 1);

        int min = 0, sec = 0, ms = 0;
        // 支持 [00:00.00] 或 [00:00:00] 格式
        if (sscanf(timeStr.c_str(), "%d:%d.%d", &min, &sec, &ms) == 3 ||
            sscanf(timeStr.c_str(), "%d:%d:%d", &min, &sec, &ms) == 3) {
            
            // 如果 ms 是 2 位（百分秒），转为毫秒
            if (ms < 100 && timeStr.find('.') != std::string::npos) {
                ms *= 10;
            }
            
            int timeMs = min * 60 * 1000 + sec * 1000 + ms;

            std::string lyric;
            if (rPos + 1 < lineStr.size()) {
                lyric = lineStr.substr(rPos + 1);
                // 去除前后空格
                size_t first = lyric.find_first_not_of(" \t\n\r");
                if (std::string::npos == first) {
                    lyric = "";
                } else {
                    size_t last = lyric.find_last_not_of(" \t\n\r");
                    lyric = lyric.substr(first, (last - first + 1));
                }
            } else {
                lyric = "";
            }

            if (!lyric.empty()) {
                list.push_back({ timeMs, lyric, NULL });
            }
        }
    }

    fclose(file);
    ESP_LOGI(TAG, "Loaded %d lines from LRC file", (int)list.size());
    return list;
}

//通过时间戳 读取歌词 （读取歌词的时间，通过时间找歌词）
std::string getCurrentLyric(int currentMS, const std::vector<LrcLine>& list) {
    if (list.empty()) return "";
    
    int index = -1;
    for (int i = 0; i < (int)list.size(); i++) {
        if (currentMS >= list[i].timeMs) {
            index = i;
        } else {
            break;
        }
    }

    if (index == -1) return "";
    return list[index].lyric;
}

void initMorningAnimationLyrics(const std::string& filePath) {
    lrcList = loadLrcFile(filePath);
    if (lrcList.empty()) {
        ESP_LOGE(TAG, "!!!歌词解析为空，请检查文件内容或时间戳格式: %s", filePath.c_str());
        return;
    }

    if (ui_MorningAnimationContainer == NULL) {
        ESP_LOGE(TAG, "ui_MorningAnimationContainer is NULL!");
        return;
    }
    
    // 隐藏原本的静态 Label，而不是清理整个容器
    if (ui_MorningAnimationLabel && lv_obj_is_valid(ui_MorningAnimationLabel)) {
        lv_obj_add_flag(ui_MorningAnimationLabel, LV_OBJ_FLAG_HIDDEN);
    }

    // 如果之前已经创建过歌词滚动容器，先删除它
    if (s_lrc_scroller && lv_obj_is_valid(s_lrc_scroller)) {
        lv_obj_del(s_lrc_scroller);
        s_lrc_scroller = NULL;
    }

    // 1. 创建一个“视口”容器 (View Port)，用于裁剪超出范围的歌词
    static lv_obj_t* s_lrc_viewport = NULL;
    if (s_lrc_viewport && lv_obj_is_valid(s_lrc_viewport)) {
        lv_obj_del(s_lrc_viewport);
    }

    s_lrc_viewport = lv_obj_create(ui_MorningAnimationContainer);
    lv_obj_remove_style_all(s_lrc_viewport);
    lv_obj_set_size(s_lrc_viewport, 280, 150); // 设置歌词显示区域的高度为 150px
    lv_obj_set_align(s_lrc_viewport, LV_ALIGN_CENTER);
    lv_obj_set_y(s_lrc_viewport, 45); // 向下偏移 45px，避开上方的图片
    lv_obj_set_style_clip_corner(s_lrc_viewport, true, 0); // 确保裁剪子对象

    // 2. 在视口内创建歌词滚动架子
    s_lrc_scroller = lv_obj_create(s_lrc_viewport);
    lv_obj_remove_style_all(s_lrc_scroller);
    lv_obj_set_width(s_lrc_scroller, lv_pct(100));
    lv_obj_set_height(s_lrc_scroller, LV_SIZE_CONTENT);
    
    // 设置垂直弹性布局
    lv_obj_set_flex_flow(s_lrc_scroller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_lrc_scroller, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_lrc_scroller, 12, 0); // 行间距

    // 遍历所有解析到的歌词，全部创建为 Label 显示出来
    for (auto& line : lrcList) {
        line.label = lv_label_create(s_lrc_scroller);
        lv_label_set_text(line.label, line.lyric.c_str());
        lv_obj_set_width(line.label, lv_pct(100)); // 占满宽度以便居中
        lv_label_set_long_mode(line.label, LV_LABEL_LONG_WRAP); // 自动换行不拆词
        lv_obj_set_style_text_align(line.label, LV_TEXT_ALIGN_CENTER, 0); // 水平居中显示
        
        // 使用指定的字体
        lv_obj_set_style_text_font(line.label, &ui_font_sfprodisplay18, 0);
        
        // 没唱到时默认颜色（浅灰色）
        lv_obj_set_style_text_color(line.label, lv_color_hex(0x606060), 0);
    }

    s_current_lrc_index = -1;
    // 初始化位置：让第一行准备在中心下方
    lv_obj_update_layout(s_lrc_scroller);
    updateMorningAnimationLyrics(0);
    
    ESP_LOGI(TAG, "歌词全部加载并显示完成，共 %d 行", (int)lrcList.size());
}

void updateMorningAnimationLyrics(int currentMS) {
    if (lrcList.empty() || s_lrc_scroller == NULL || !lv_obj_is_valid(s_lrc_scroller)) return;

    int index = -1;
    // 增加 50ms 预判补偿
    int searchTime = currentMS + 50; 
    for (int i = 0; i < (int)lrcList.size(); i++) {
        if (searchTime >= lrcList[i].timeMs) {
            index = i;
        } else {
            break;
        }
    }

    if (index != -1 && index != s_current_lrc_index) {
        // 高亮当前行，恢复旧行
        if (s_current_lrc_index != -1 && s_current_lrc_index < (int)lrcList.size()) {
            lv_obj_set_style_text_color(lrcList[s_current_lrc_index].label, lv_color_hex(0x808080), 0);
        }
        lv_obj_set_style_text_color(lrcList[index].label, lv_color_hex(0xFFFFFF), 0);

        // 计算滚动位置，使当前行在“视口”中居中
        lv_obj_update_layout(s_lrc_scroller); // 确保坐标已更新
        
        lv_obj_t* viewport = lv_obj_get_parent(s_lrc_scroller);
        int viewport_h = lv_obj_get_height(viewport);
        int label_y = lv_obj_get_y(lrcList[index].label);
        int label_h = lv_obj_get_height(lrcList[index].label);
        
        // 目标位置：视口中心 - 歌词行中心
        int target_y = (viewport_h / 2) - (label_y + label_h / 2);
        
        // 动画滚动
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_lrc_scroller);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&a, lv_obj_get_y(s_lrc_scroller), target_y);
        lv_anim_set_duration(&a, 300);
        lv_anim_start(&a);

        s_current_lrc_index = index;
    }
}
