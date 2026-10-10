# ZySU

<img align="right" src="../ZySU-mini.png" width="180" alt="ZySU ロゴ">

ZySU は [SukiSU-Ultra](https://github.com/ShirkNeko/SukiSU-Ultra) から派生した、KernelSU ベースの Android root ソリューションです。ARM64 カーネルモジュール、C++17 のユーザー空間コンポーネント、Kotlin / Jetpack Compose の Manager を含みます。

[English](../README.md) · [简体中文](../zh/README.md) · **日本語** · [Türkçe](../tr/README.md) · [Русский](../ru/README.md)

## 現在の機能

- カーネル `su`、アプリごとの権限とプロファイル、Manager の署名検証、動的 Manager、SuperKey 認証。
- モジュールのインストール、有効化・無効化・削除、起動スクリプト、WebUI、リポジトリ管理。
- ksud 内蔵の SUMHP による OverlayFS、Magic Mount、SUMH、マウントなしの選択。`auto` は条件に合う OverlayFS を優先し、次に Magic Mount を試します。SUMH は明示的に選択します。外部 MetaModule のライフサイクルも扱います。
- 起動イメージの修正・復元、LKM の直接注入、パーティション操作、AnyKernel3、ramdisk 編集。
- SUMH のパス・マウント表示制御、ADB root、su ログ、環境状態の設定と診断。

機能の実装は、すべての端末、モジュール、環境検査との互換性を保証するものではありません。

## 対応範囲

| 項目 | 条件 |
| --- | --- |
| アーキテクチャ | ARM64 / `arm64-v8a` |
| Manager / userspace | Android 12（API 31）以降 |
| カーネル | Linux 6.1 以降。配布 LKM は下記の Android GKI KMI 向け |
| ビルド形式 | `CONFIG_KSU=m` のみ。`CONFIG_KSU=y` は非対応 |
| 標準 KMI | `android14-6.1`、`android15-6.6`、`android16-6.12`、`android17-6.18` |

これはビルド対象の一覧であり、動作確認済み端末の一覧ではありません。Android のバージョンだけで KMI を判断しないでください。古いカーネル、他の KMI、non-GKI 端末は配布物の対応範囲外です。boot への直接注入も LKM を使用し、built-in 対応を意味しません。

## ドキュメントと入手先

- [インストール](guide/installation.md)：イメージの選択、導入、更新、復旧。
- [LKM のビルドと統合](guide/how-to-integrate.md)。
- [Actions とローカルビルド（英語）](../guide/workflow-build.md)。
- [ksud CLI](../ksud-cli.md)、[ramdisk プロトコル](../ramdisk-editor-protocol.md)、[技術文書一覧（中国語）](../zh/README.md)。
- [公開リリース](https://github.com/wuluoy-creator/ZySU/releases)と[不具合報告](https://github.com/wuluoy-creator/ZySU/issues)。

Manager は公開リリースの更新を確認します。CI 更新チャンネルはありません。Actions では最終 APK の `Manager-arm64-v8a` を使用します。APK の更新だけでは起動イメージ内のカーネルモジュールは更新されません。

不具合報告には端末・ファームウェア、`uname -r`、KMI、Manager / ksud / カーネルのバージョン、導入方法、再現手順とログを添えてください。ホストテストは実機の起動・復旧確認を代替しません。

## ライセンスと謝辞

[kernel/LICENSE](../../kernel/LICENSE) は GPL v2、ルートの [LICENSE](../../LICENSE) は GPL v3 です。個別ファイルと依存ライブラリのライセンスも適用されます。WebUI ライブラリは Apache-2.0 を宣言しています。

KernelSU、SukiSU-Ultra、Magisk および[同梱コンポーネント](../../userspace/ksud/third_party/README.md)の開発者に感謝します。詳細は[プロジェクト概要](../../README.md)を参照してください。
