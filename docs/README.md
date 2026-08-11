# Documentation

这里是 EEGO A4 模板的文档入口。第一次接触项目时按“开始开发”顺序阅读；只有
修改底层驱动或准备发布时才需要维护者文档。

## 开始开发

1. [从零开发指南](GETTING_STARTED.md)：安装、构建、备份和第一个示例。
2. [可编译示例](../examples/README.md)：五个最小、独立构建的外设示例。
3. [驱动与 API](DRIVER_GUIDE.md)：显示、触控、SD、RTC、电池、无线和深睡。
4. [项目架构](ARCHITECTURE.md)：分层、总线所有权、坐标和刷新模型。

## 硬件参考

- [逆向硬件参考](HARDWARE_REFERENCE.md)：证据来源、引脚、控制器和置信度。
- [官方/CrossLink/模板对照](OEM_CROSSLINK_MAPPING.md)：实现选择理由。
- [安全显示区域](SAFE_AREA.md)：R60 外轮廓、R48 内容区与矩形 UI 合同。

## 测试、调试与恢复

- [完整诊断固件](DIAGNOSTICS_GUIDE.md)
- [Framebuffer 实机回读](FRAMEBUFFER_DEBUGGING.md)
- [常见问题](TROUBLESHOOTING.md)
- [安全刷写与救砖](RECOVERY.md)
- [实体设备验证摘要](VALIDATION.md)

## 维护项目

- [维护者与发布指南](MAINTAINER_GUIDE.md)
- [贡献规范](../CONTRIBUTING.md)
- [安全政策](../SECURITY.md)
- [变更记录](../CHANGELOG.md)

## 文档约定

- `已验证`：在实体 EEGO A4 或可重复构建中观察到。
- `二进制确认`：由已记录 SHA-256 的固件分析得到，但未必覆盖所有硬件版本。
- `推断`：有支持证据，仍需新设备或仪表验证。
- `未知/不存在证据`：不得据此增加猜测性驱动。

文档中的 `<PORT>`、`<project>` 和 `<backup>` 都是占位符，不应原样执行。
