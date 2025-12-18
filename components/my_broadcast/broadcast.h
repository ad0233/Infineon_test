#pragma once

#include <vector>
#include <string>
#include "lvgl.h"

struct LrcLine {
    int timeMs;         // 时间 ms
    std::string lyric;  // 歌词
    lv_obj_t* label;    // 对应的 UI 标签
};

extern std::vector<LrcLine> lrcList;

/**
 * @brief 从文件读取 LRC 歌词
 * 
 * @param filePath 文件路径 (例如 "/sdcard/test.lrc")
 * @return std::vector<LrcLine> 解析后的歌词列表
 */
std::vector<LrcLine> loadLrcFile(const std::string& filePath);

/**
 * @brief 根据当前播放时间获取对应的歌词
 * 
 * @param currentMS 当前播放时间 (ms)
 * @param list 歌词列表
 * @return std::string 对应的歌词文本
 */
std::string getCurrentLyric(int currentMS, const std::vector<LrcLine>& list);

/**
 * @brief 初始化早安动画的歌词显示
 * 
 * @param filePath LRC 文件路径
 */
void initMorningAnimationLyrics(const std::string& filePath);

/**
 * @brief 更新早安动画的歌词滚动
 * 
 * @param currentMS 当前播放时间 (ms)
 */
void updateMorningAnimationLyrics(int currentMS);

