# カーネルモジュールのビルド

ZySU は ARM64 のロード可能モジュール `kernelsu.ko` として動作します。サポートする設定は `CONFIG_KSU=m` です。`CONFIG_KSU=y` による組み込みはサポートしません。[Kbuild](../../../kernel/Kbuild) は Linux 6.1 未満と ARM64 以外を拒否します。

## ビルド対象

DDK ワークフローとローカルスクリプトの対象は次のとおりです。

| KMI | カーネル系列 |
| --- | --- |
| `android14-6.1` | 6.1 |
| `android15-6.6` | 6.6 |
| `android16-6.12` | 6.12 |
| `android17-6.18` | 6.18 |

これはビルド対象であり、動作確認済み端末の一覧ではありません。同じバージョンでも KMI、設定、シンボル、モジュール読み込みポリシーによって互換性が変わります。[Actions ガイド（英語）](../../guide/workflow-build.md) と [インストールガイド](installation.md) を参照してください。

## 端末に対応するカーネルでビルドする

Linux、Make、Clang/LLVM、Git、および対象カーネルのツールチェーンが必要です。先に端末の設定でカーネルをビルドし、生成済みヘッダー、シンボル情報、シンボルを削除していない `vmlinux` を保持してください。ソースの取得や `modules_prepare` だけでは不十分です。

ZySU のリポジトリ全体が必要です。`kernel/include/uapi` は `../../uapi` への Git シンボリックリンクです。Windows Git が通常のテキストファイルとしてチェックアウトした場合、Linux/WSL 環境で実際のリンクを保持するチェックアウトを使用してください。

リポジトリのルートで、例のパスを実際の絶対パスに置き換えて実行します。

```sh
export KDIR="/absolute/path/to/kernel/out"
export CLANG_PATH="/absolute/path/to/clang/bin"
export PATH="$CLANG_PATH:$PATH"
export ARCH=arm64
export LLVM=1
export LLVM_IAS=1
export CROSS_COMPILE=aarch64-linux-gnu-

test -f "$KDIR/vmlinux"
test -f kernel/include/uapi/supercall.h
make -C kernel CONFIG_KSU=m CONFIG_KSU_SUPERKEY=y CC=clang
llvm-strip -d kernel/kernelsu.ko
```

`KDIR` は設定とビルドを済ませたカーネルのディレクトリです。`O=out` を使った場合は通常、出力ディレクトリを指定します。[Makefile](../../../kernel/Makefile) は外部モジュールをビルドし、`check_symbol` で `vmlinux` と照合します。生成物は `kernel/kernelsu.ko` です。この検査は実機での読み込み、起動、動作の検証ではありません。

TSR は `CONFIG_HAVE_SYSCALL_TRACEPOINTS` を使用します。モジュール、トレース、シンボル設定を Hook 実装と照合してください。`CONFIG_KRETPROBES` は tracepoint marker の kretprobe 経路を有効にしますが、無効時のフォールバックもあります。実験的な `CONFIG_KSU_KRETPROBES_SUCOMPAT` は `KRETPROBES` に依存します。

## 設定と署名

設定は [Kconfig](../../../kernel/Kconfig) にあります。上の例は SuperKey を明示的に有効にします。`CONFIG_KSU_DISABLE_MANAGER=y` は Manager 連携と SuperKey を、`CONFIG_KSU_DISABLE_POLICY=y` はアプリ別プロファイル設定を無効にします。

独自の APK 署名を別途ビルドしたモジュールが自動的に信頼するわけではありません。Kbuild は公開証明書の `KSU_MANAGER_CERT_SIZE` と `KSU_MANAGER_CERT_SHA256` を組で受け取ります。Actions との連携は [ビルドガイド（英語）](../../guide/workflow-build.md) を参照してください。

バージョンの上書きには `ZYSU_KERNEL_VERSION_NAME` と `ZYSU_KERNEL_VERSION_CODE` を使い、検証に Python 3 が必要です。上書きしない場合は Git 情報を使い、GitHub にアクセスする場合もあります。APK 全体のローカルビルド入口は [build.sh](../../../scripts/build.sh) と [build.bat](../../../scripts/build.bat) です。
