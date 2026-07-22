#pragma once

class AchievementsStore;
class ReadingStatsStore;

namespace JsonSettingsIO {

bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);

bool saveAchievements(const AchievementsStore& store, const char* path);
bool loadAchievements(AchievementsStore& store, const char* json);
bool loadAchievementsFromFile(AchievementsStore& store, const char* path);

}  // namespace JsonSettingsIO
