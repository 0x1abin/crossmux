# Contributing

感谢你帮助改进 EEGO A4 模板。这个项目直接控制电源、Flash 和电子纸波形，因此
“能编译”只是最低要求；每项硬件变更都必须能说明证据、风险和恢复方式。

## 开始开发

```sh
git clone <your-fork>
cd eego-a4-template-firmware
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python scripts/validate_project.py --quick
pio run -e eego_a4_quickstart
```

目录职责见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)，完整文档导航见
[`docs/README.md`](docs/README.md)。

## 提交要求

- 一个提交只解决一个可解释的问题。
- 应用代码不得散落硬件引脚；板级事实进入 `BoardConfig` 或
  `EegoA4Support`。
- 普通 UI 必须使用 `useUiContentRect()`；越过安全区只能用于有注释的背景或
  边缘测试。
- 不要提交 `.pio/`、`release/`、整片 Flash 备份、设备 MAC、Wi-Fi 密码或
  未明确许可的固件/字体。
- 新增依赖必须锁定版本并说明许可证。
- 修改引脚、波形、触控固件、分区或电源顺序时，必须同步更新硬件文档、恢复
  文档和验证证据。

## 本地检查

提交前至少执行：

```sh
python scripts/validate_project.py
python -m compileall -q scripts
```

若改动打包流程，还应执行：

```sh
pio run -e eego_a4_diagnostics
python scripts/package.py
cd release/1.0.18  # 替换为打包器刚刚输出的版本目录
shasum -a 256 -c SHA256SUMS
```

## 真机变更

在真机写入前：

1. 保存新的 16 MiB 整片备份和 SHA-256。
2. 运行发布包的 `--preflight-only`。
3. 记录设备硬件变体（尤其是否存在 LM3630A 前光）。
4. 优先 app-only 更新；只有分区表不匹配时才执行完整首装。
5. 写后运行相关单项测试和 `run all`，并保存串口输出与 framebuffer。

不要在 Pull Request 中上传私有整片备份。可以上传裁剪后的日志、无设备身份信息
的 JSON，以及必要的屏幕照片或 framebuffer PNG。

## Pull Request

说明：

- 问题与设计选择；
- 影响的硬件版本；
- 已运行的构建环境和真机测试；
- 是否改变分区、LUT、触控表或电源行为；
- 恢复方法；
- 新增数据或代码的来源与许可证。

维护者发布前还会执行
[`docs/MAINTAINER_GUIDE.md`](docs/MAINTAINER_GUIDE.md) 中的检查。
