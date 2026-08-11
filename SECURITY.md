# Security Policy

## Supported version

安全修复只保证应用于默认分支和最新发布版本。历史二进制用于研究和恢复，不再
单独维护。

## Reporting

请不要公开提交可导致任意 Flash 写入、绕过目标身份检查、泄露凭据或使设备无法
恢复的漏洞。优先使用 GitHub 仓库的 **Security → Report a vulnerability**
私密报告功能，并提供：

- 受影响的版本或提交；
- 可复现步骤和最小日志；
- 是否需要实体 EEGO A4；
- 可能的影响；
- 建议修复（如有）。

不要附带整片设备备份、MAC、Wi-Fi 密码或其他个人数据。普通功能缺陷请使用
Issue 模板。

## Hardware warning

第三方固件可能永久改变分区、NVS、校准信息或电源行为。所有写入都应先保存新的
16 MiB 备份，并通过 `safe_flash_template.py` 的预检、设备确认和写后校验。
