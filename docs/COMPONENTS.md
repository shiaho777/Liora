# 组件与目录签名

Liora 的内核、迁移器等大组件按需下载。Core 不联网：它只负责计划、校验与安装，壳负责实际的 HTTP 下载与解包。

## 安装流程

1. 界面调用 `components.plan`，Core 根据 `components.json` 与当前平台返回：
   - 归档 URL 与大小；
   - 每个文件在解包后的相对路径、大小与 SHA-256；
   - staging 目录。
2. 壳下载归档，校验 HTTP 状态与归档大小。
3. 壳用 `tar` 解包到 staging。
4. 壳调用 `components.commit`。Core 逐文件检查路径、大小与 SHA-256，全部通过后才移入版本目录并写入 `components/index.json`。
5. 任一检查失败都会拒绝安装并清理 staging，不会留下半安装状态。

> 归档 URL 应使用 HTTPS。文件哈希保证归档内容与目录一致；目录签名保证目录本身没有被篡改。

## 目录签名

目录可以带一个 `signature` 字段：

```json
{
  "version": 1,
  "components": [ ... ],
  "signature": "<base64 或 hex 的 64 字节 Ed25519 签名>"
}
```

签名覆盖的字节是 **去掉 `signature` 后** 的规范 JSON：

- UTF-8；
- 对象键按字典序排序（与 nlohmann `json::dump()` 一致）；
- 无多余空白（紧凑分隔符 `,` 与 `:`）；
- 整数不加引号。

公钥为 32 字节，可用 hex 或 base64。配置方式：

- 环境变量 `LIORA_UPDATE_KEY`；
- 或 `Core::Options::updateKey`（测试/嵌入方使用）。

未配置公钥时，Core 接受未签名目录（开发构建）。一旦配置，缺少签名、签名错误或被修改过的目录都会被拒绝；已加载的旧目录保持不变，组件不会出现在下载列表，`components.plan` 也不会返回计划。

## 当前状态

Ed25519 校验使用公有领域的 TweetNaCl（`third_party/tweetnacl`，20140427），由 `core/src/crypto.cpp` 封装。单元测试覆盖 RFC 8032 向量、有效/无效签名、被篡改目录与 Core 启动时的拒绝行为。
