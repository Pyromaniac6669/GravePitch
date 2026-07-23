# GravePitch 简体中文字体子集

- 来源：Google Fonts 官方仓库 `ofl/notosanssc/NotoSansSC[wght].ttf`
- 下载地址：`https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf`
- 源文件 SHA-256：`a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`
- 许可证：SIL Open Font License 1.1，见 `OFL-NotoSansSC.txt`
- 生成工具：fonttools `4.59.0`
- 输出文件：`GravePitchCjkSubset.ttf`
- 输出 SHA-256：`c4e8b0dcbb4f7bb93db054b0ab71cb50ad6cfb39cf6c7058afc872a0af8b95a7`

先将可变字体固定为 `wght=400`，再把内部字体家族名改为 `GravePitch CJK`，最后仅保留：

- `U+0020..U+007E` 基本拉丁字符。
- `U+4E2D,U+4E3A,U+4E49,U+4F53,U+4FDD,U+51C6,U+5B58,U+5B8C,U+5B9A,U+5F26,U+6210,U+6587,U+6821,U+7B80,U+7F6E,U+81EA,U+8A00,U+8BBE,U+8BED,U+8C03,U+9884`。

修改 `resources/i18n/zh-Hans.txt` 中的中文文案时，必须重新收集非 ASCII 码点、生成字体子集并更新本文件中的字符集与 SHA-256。
