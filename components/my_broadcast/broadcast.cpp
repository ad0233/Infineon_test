#include "broadcast.h"
#include <cstdio>
#include <cstring>
#include "esp_log.h"

static const char *TAG = "LRC";

std::vector<LrcLine> lrcList;

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
            } else {
                lyric = "";
            }

            list.push_back({ timeMs, lyric });
        }
    }

    fclose(file);
    ESP_LOGI(TAG, "Loaded %d lines from LRC file", (int)list.size());
    return list;
}

//通过时间戳 读取歌词 （读取歌词的时间，通过时间找歌词）
std::string getCurrentLyric(int currentMS, const std::vector<LrcLine>& list) {
    static size_t index = 0;
    if (list.empty()) return "";

    // 如果时间回退（比如拖动进度条），重置索引
    if (index > 0 && currentMS < list[index - 1].timeMs) {
        index = 0;
    }

    // 向后寻找匹配当前时间的歌词行
    while (index + 1 < list.size() && currentMS >= list[index + 1].timeMs) {
        index++;
    }

    return list[index].lyric;
}
