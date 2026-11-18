# 实现说明

## 数据文件

- `/sdcard/database/oxford.db`：原词库，只读。
- `/sdcard/fsrs_progress.db`：应用创建，保存卡片、设置、等级进度和汇总。
- `/sdcard/audio/{uk,us}_audio_split_24m/`：发音文件。

## 进度表

### cards

每个单词最多三行，分别对应 mode 1（英→中）、mode 2（中→英）、mode 3（默写）。每行拥有独立的 FSRS 状态：

- `state`: 0 New, 1 Learning, 2 Review, 3 Relearning
- `due`: Unix 秒
- `stability`: FSRS 稳定性，单位天，也是“熟练度”门槛
- `difficulty`: FSRS 难度
- `elapsed_days`, `scheduled_days`: 调度公式输入
- `reps`, `lapses`, `last_review`
- `enabled`: 默写卡在认读双卡达到门槛前为 0

### started_words / word_status

`started_words` 记录单词初次引入时间和 CEFR。它用于每日新词配额。

`word_status` 是一个小型的派生状态表，保存认读双卡是否达标、默写是否解锁、单词是否熟练。这样等级进度可以直接按 CEFR 统计，避免每次复习扫描几千张卡。

### settings

固定 FSRS 默认参数，运行时可调：

- 每天新词数，默认 15 个词
- 单次会话卡片上限，默认 60 张
- 音量
- 认读解锁默写门槛，默认 21 天
- 默写熟练门槛，默认 21 天
- 等级解锁比例，默认 90%
- `write_review_log`，默认 0

`review_log` 从首次启动就创建，但默认不插入行。打开设置开关后才累计逐次审查记录。

## 派发

1. 先取所有到期的 Relearning / Learning / Review 卡片。
2. 再取已激活的 New 卡片。
3. 如果还有容量，按 A1→A2→B1→B2→C1 和词库 `id` 顺序引入新词，每个新词创建 mode 1、2 两张可学卡，以及一张 disabled 的 mode 3 卡。
4. 每日配额按“词”计算，不按卡片计算。
5. 学习步长在 Learning/Relearning 状态下使用 1 分钟、5/10 分钟短步长；进入 Review 后使用 FSRS 间隔。

## 熟练和升级

- mode 1 与 mode 2 的 stability 都达到 `recognition_gate_days` 后，启用该词的 mode 3。
- mode 3 stability 达到 `dictation_gate_days` 后，`word_status.mastered=1`。
- 当前 CEFR 的熟练词比例达到 `level_unlock_ratio` 后，把 `unlocked_level` 单调推进到下一级。
- 解锁不回锁。默写卡自己的 Relearning 状态负责处理后来遗忘。

## TF 卡写入策略

这里没有启用 WAL。WAL 主要改善并发和写入原子性，不是磨损均衡；它会额外产生 `-wal`/`-shm` 文件，并在 checkpoint 时继续写数据页。对本应用这种单进程、低频小事务，收益不大。

当前使用：

```sql
PRAGMA journal_mode=PERSIST;
PRAGMA synchronous=FULL;
PRAGMA temp_store=MEMORY;
```

每次用户评分只提交一个小事务，日志默认关闭。还会写一个每天一行的 `daily_stats` 汇总。按每天 100 张卡估算，一年约 3.6 万次小事务；SD 卡内部的 FTL 会做磨损均衡，真正影响寿命的是总写入量、掉电和随机写，而 WAL 不会消除这些成本。

后续如果要进一步降低写入：

- 保持审查日志关闭。
- 空闲时不做 `VACUUM`。
- 不要每次触摸事件写库，只在评分提交时写。
- 迁移到手机时可以直接导出 `cards`、`word_status` 和 `review_log`（若已开启）。

## 音频和键盘

- UK/US 按钮分别在 `/sdcard/audio/uk_audio_split_24m/` 和 `/sdcard/audio/us_audio_split_24m/` 查找文件，找不到时兼容根目录回退。
- 键盘使用 I2C1：SDA GPIO0、SCL GPIO1、INT GPIO50、地址 0x6D，字符模式。
- 默写题目只显示对应字母数量的格子；中文/英文释义和词性作为提示，横线每格对应一个字母。

## 当前限制

- 未实现深度睡眠和 RTC 定时唤醒；RTC 目前用于提供正确的系统时间。
- 真机上的屏幕方向、触摸坐标、键盘字符模式和音频音量仍需上机校准。
- 缺音频的单词会提示“发音文件不存在”，不影响调度。

