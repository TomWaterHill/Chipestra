# Chipestra（チップエストラ）

Chipestra は、"chip" と "orchestra" を合わせた名前の、サウンドハードウェア・
プレーヤーです。往年のアーケードゲームの音楽を、録音の再生ではなく、当時の
サウンドハードウェア（サウンド用CPUとサウンドチップ）を、ゲーム自身の ROM から
エミュレートして再生します。コンソールの基板への対応も、予定しています。

Chipestra は、作曲ツールではありません。また、VGM などの、あらかじめ作られた
音楽ファイルのプレーヤーでもありません。

バージョン: Ver 1.001.00

## 対応基板と確認済みタイトル

Chipestra は、対応している基板上のゲームを、XML定義ファイルを書くことで
追加できるように設計されています（[docs/xml-format.md](docs/xml-format.md) を参照。英語）。
ここに挙げたタイトルは、作者が確認したものです。同じハードウェア系列でも、
載っていないタイトルは、確認していません。

- Sega X-Board: After Burner II（アフターバーナーII）、GP Rider、Thunder Blade（サンダーブレード）
- Sega Out Run: Out Run（アウトラン）
- Konami Gyruss: Gyruss（ジャイラス）
- Konami Bubble System: Bubble System BIOS
- Konami GX400: Gradius（グラディウス）
- Taito Ninja Warriors: The Ninja Warriors（ザ・ニンジャウォリアーズ）
- Taito F2 System: Metal Black（メタルブラック）
- Taito Darius: Darius（ダライアス）
- Data East Caveman Ninja Hardware: Vapor Trail（空牙）
- Namco System 1: Dragon Spirit（ドラゴンスピリット）
- Capcom 68000 Based: Street Fighter（ストリートファイター。US版のROMセット sf のみ対応。BGM、効果音、ボイスを再生できます）

## 機能

- チャンネルごとのレベルメーター、パン、ミュート、ソロ
- マスター音量とレベルメーター
- 出力サンプルレートの選択（既定は自動。共有モードでは、Windowsのサウンド
  設定が優先されます）
- WASAPI 共有モードでの出力
- 自然に終わる曲は、終わると自動で停止
- ゲームと曲の一覧は XML ファイルで定義されているため、ご自身で追加や修正が
  できます（[docs/xml-format.md](docs/xml-format.md) を参照。英語）

## 必要なもの

- Windows 11（64bit）。Windows 11 でのみ動作を確認しています。
- .NET 10 Desktop Runtime（x64）。Microsoft のサイトから入手できます:
  https://dotnet.microsoft.com/download/dotnet/10.0
  入っていない場合は、Chipestra の起動時に、Windows がダウンロード先の案内を
  表示します。
- 再生したいゲームの ROM ファイル。Chipestra には、ROM、BIOS イメージ、その他の
  ゲームデータは一切含まれていません。ご自身で用意し、そのフォルダを
  Chipestra に指定してください（Settings > ROM Root）。

## ダウンロードと起動

1. このリポジトリの Releases のページから、Chipestra-1.001.00.zip を
   ダウンロードします。
2. 展開すると、Chipestra-1.001.00 というフォルダができます。
3. そのフォルダの Chipestra.exe を起動します。
4. Settings を開き、ROM Root に、ROM ファイルのあるフォルダを指定します。
   サブフォルダの中も探します。

実行ファイルにはコード署名がないため、初回の起動で、Windows SmartScreen の
警告が出ることがあります（「詳細情報」→「実行」）。

設定は %LOCALAPPDATA%\Chipestra\settings.json に保存されます。Chipestra を
削除するときは、展開したフォルダと、%LOCALAPPDATA%\Chipestra フォルダを
削除してください。

## ソースからのビルド

ツール（このプロジェクトは、次の環境でビルドと動作確認をしています）:

- Windows 11（64bit）と PowerShell 7 以降
- Visual Studio 2026（18.x）。ワークロード「C++ によるデスクトップ開発」と
  「.NET デスクトップ開発」、およびコンポーネント「vcpkg パッケージ マネージャー」
- .NET SDK 10（プロジェクトは net10.0 / net10.0-windows が対象。10.0.401 でビルド）
- CMake 3.24 以降（PATH に通す。CMakeLists.txt の要求は 3.24。4.4.2 でビルド）
- vcpkg: Native のビルドは vcpkg のマニフェストモードを使います
  （src/Chipestra.Native/vcpkg.json に libsoxr が書かれ、ベースラインで版が
  固定されています）。ビルド用のスクリプトは、Visual Studio に同梱の vcpkg、
  または VCPKG_ROOT が指す vcpkg を使います。libsoxr は、最初の構成（configure）
  のときにダウンロードしてビルドされるため、そのときはインターネット接続が
  必要です。

すべてのビルドとテスト（Release、x64）は、リポジトリの直下で、PowerShell から
次のコマンドを実行します:

    .\tools\verify.ps1

Native ライブラリを CMake でビルドし、Native のテストを実行し、Chipestra.slnx を
dotnet でビルドし、マネージドのテストを実行し、ビルド出力の整合を確認します。
最後の確認は、作業ツリーにコミットされていないファイルや未追跡のファイルが
あると失敗します。アプリケーションは
src\Chipestra.App\bin\Release\net10.0-windows\Chipestra.exe にできます。

配布用のパッケージを作るには（先に verify.ps1 を実行します）:

    .\tools\package-release.ps1 -Zip

dist\Chipestra-<バージョン>\ と dist\Chipestra-<バージョン>.zip ができます
（バージョンは version.props から決まります）。

ROM を使うテストは任意です。ROM ファイルのあるフォルダを、verify.ps1 を
実行する前に、環境変数 CHIPESTRA_ROM_ROOT に設定してください。設定しない場合、
それらのテストはスキップされます。

## 貢献とサポート

不具合の報告は、歓迎します。コードの貢献は、当面、受け付けていません。

ROM の入手方法は、質問しないでください。Chipestra は ROM を提供せず、
入手方法の質問にも、お答えしません。

## ライセンス

Chipestra はフリーソフトウェアで、GNU General Public License バージョン2、
または（お好みで）それ以降のバージョンのもとで公開されています。
[LICENSE](LICENSE) をご覧ください。

サードパーティのコードとライブラリは [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) に、
ライセンス全文は [licenses/](licenses/) フォルダにあります。同梱している
libsoxr ライブラリ（soxr.dll）のソースコードは、Releases のページで、別の
ファイルとして提供しています。

Copyright (C) 2026 Tom Water Hill

## 謝辞

Chipestra の CPU とサウンドチップの実装は、多くの開発者の仕事、特に MAME
プロジェクトを参考に、移植・適応したものです。著作権者の一覧は
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) をご覧ください。

## 免責

Chipestra は独立したプロジェクトで、MAME プロジェクトやゲームの発売元とは
関係がなく、承認も受けていません。ゲームのタイトルと商標は、それぞれの
所有者に帰属します。
