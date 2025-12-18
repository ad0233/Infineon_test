#pragma once

#include <vector>
#include <string>

struct LrcLine {
    int timeMs;         // 时间 ms
    std::string lyric;  // 歌词
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

