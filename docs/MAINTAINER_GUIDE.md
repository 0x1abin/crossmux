# Maintainer and release guide

本指南面向准备合并底层改动或发布二进制的维护者。普通应用开发者只需阅读
[`GETTING_STARTED.md`](GETTING_STARTED.md)。

## 1. Source-tree policy

源码仓库应保持可审查、可重建：

- 提交 `src/`、`lib/`、`examples/`、`scripts/`、`docs/` 和项目元数据。
- 不提交 `.pio/`、`release/`、`captures/`、整片备份、ELF、map、NVS、设备
  MAC、凭据、第三方固件或未明确许可的字体。
- GitHub Release 承载由当前 tag 重建的二进制包；源码提交不承载历史发布包。
- 大于 1 MiB 的新文件必须说明必要性、来源、许可证和为何不能生成。

快速审计：

```sh
git status --short
find . -type f -size +1M -not -path './.git/*' -print
rg -n '/Users/|/home/|[A-Za-z]:\\\\Users\\\\' . \
  --glob '!.git/**' --glob '!release/**' --glob '!.pio/**'
```

## 2. Version and changelog

1. 在 `lib/EegoA4Support/include/EegoA4Hardware.h` 更新
   `TEMPLATE_VERSION`。
2. 在 `CHANGELOG.md` 将 Unreleased 内容归入新版本和发布日期。
3. 不在 README 或脚本中复制新的版本常量；打包器从头文件读取版本。
4. 若改变公开 API、分区、硬件合同或交互语义，按 Semantic Versioning 选择
   版本。

## 3. Automated checks

在干净环境执行：

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python scripts/validate_project.py
python -m compileall -q scripts
```

`validate_project.py` 会检查版本、目录合同、示例安全启动、UI 安全区、Markdown
链接、开源元数据，并编译六个环境。全部通过后才能进入真机阶段。

## 4. Real-device regression

写入前创建新的、仓库外 16 MiB 备份。至少检查：

- 启动电源保持和 USB CDC；
- 菜单与单项测试停留逻辑；
- Full / Fast / gray / black / white；
- 九宫格触控、边界、屏下键短/长按和三个侧键；
- SD 临时文件、RTC、ADC、电池和三阶段充电；
- Wi-Fi、BLE、I²C ACK 与可选前光分支；
- 中英文字体三页；
- `run all`、JSON 报告和 framebuffer 完整回读；
- 默认永不休眠；显式深睡按定时器唤醒。

使用：

```sh
python scripts/capture_framebuffer.py --port <PORT> \
  --page-command 'font page 3' --name font-page-3
```

将脱敏后的结论和资源摘要写入 `docs/VALIDATION.md`。不要提交原始整片备份。

## 5. Package and verify

```sh
pio run -e eego_a4_diagnostics
python scripts/package.py
cd release/<version>
shasum -a 256 -c SHA256SUMS
python safe_flash_template.py --port <PORT> --preflight-only
```

在一台有恢复备份的设备上分别验证适用的 app-only 或 first-install 流程。发布
资产应包含 `README.md`、`SHA256SUMS`、`manifest.json`、三份许可证/来源
声明、安全刷写器、app、full image、bootloader、partition table 和 boot_app0，
且不含多余文件。

## 6. Provenance and legal review

发布前逐项确认：

- 新代码与依赖许可证可兼容；
- 新数据、波形、固件表和字体有来源与摘要；
- 没有把第三方完整固件、字体或商标资源误标为 MIT；
- `NOTICE.md` 仍准确；
- 二进制衍生互操作数据的公开分发已经由发布者独立评估。

最后一项不能由构建测试替代。本项目记录技术来源，但不提供法律结论。

## 7. GitHub release

1. 从已通过完整构建和真机回归的提交创建签名 tag。
2. 从该 tag 重新构建并打包，不复用未知来源的旧目录。
3. 上传 `release/<version>/` 文件作为 GitHub Release assets。
4. 在 release notes 链接 changelog、验证摘要、刷写与恢复说明。
5. 从空目录下载发布资产并再次验证 `SHA256SUMS`。
