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

公钥为 32 字节，可用 hex 或 base64。默认公钥在 `data/update-key.pub`，构建时嵌入 Core。私钥默认保存在仓库外：

```
~/.config/liora/update-key.ed25519   (32 字节 seed，权限 0600)
```

**请备份私钥；丢失后将无法再为现有公钥签发目录，只能轮换公钥并重新签名。** 私钥不要提交到仓库或 CI 日志。

### 生成与签名

```bash
# 只在首次或轮换时执行；--force 会覆盖现有密钥
python3 scripts/generate-update-key.py

# 签名目录（默认使用上面的私钥）
python3 scripts/sign-components.py components.json --in-place
```

生成脚本会把公钥写入 `data/update-key.pub`，重新构建后生效。轮换时应同时更新目录签名并重新发布客户端。

### 覆盖与开发

- `LIORA_UPDATE_KEY`（hex/base64）覆盖内置公钥；
- `Core::Options::updateKey` 供测试/嵌入方使用；
- `LIORA_DISABLE_COMPONENT_SIGNATURES=1` 临时接受未签名目录，仅用于本地开发。

一旦配置公钥（默认构建即是），缺少签名、签名错误或被修改过的目录都会被拒绝；已加载的旧目录保持不变，组件不会出现在下载列表，`components.plan` 也不会返回计划。

当前内置公钥指纹：`ec0370237e5ff5ad`（SHA-256 前 16 位）。

## 当前状态

Ed25519 校验使用公有领域的 TweetNaCl（`third_party/tweetnacl`，20140427），由 `core/src/crypto.cpp` 封装。单元测试覆盖 RFC 8032 向量、有效/无效签名、被篡改目录与 Core 启动时的拒绝行为。
