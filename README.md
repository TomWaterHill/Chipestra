# Chipestra

[English](#english) | [日本語](#日本語)

## English

Chipestra (pronounced "chip-es-tra", from "chip" and "orchestra") plays the
music of classic arcade games on Windows. Instead of playing back recordings,
it runs each game's own sound program on emulated sound hardware (the sound
CPUs and sound chips of the original board), using ROMs you supply yourself.
Support for home consoles is planned.

Chipestra is not a music composition tool, and it does not play VGM or other
pre-recorded music files. No ROMs are included.

Version: Ver 1.001.00

### Supported Boards & Verified Titles

Chipestra is designed so that additional games on a supported board can be
added by writing an XML definition file (see [docs/xml-format.md](docs/xml-format.md)).
The titles listed below are the ones the author has checked. Other titles are
not verified, even if they run on the same hardware family.

- Sega X-Board: After Burner II, GP Rider, Thunder Blade
- Sega Out Run: Out Run
- Konami Gyruss: Gyruss
- Konami Bubble System: Bubble System BIOS
- Konami GX400: Gradius
- Taito Ninja Warriors: The Ninja Warriors
- Taito F2 System: Metal Black
- Taito Darius: Darius
- Data East Caveman Ninja Hardware: Vapor Trail
- Namco System 1: Dragon Spirit
- Capcom 68000 Based: Street Fighter (US version only: the ROM set "sf"; music, sound effects and voices)

### Features

- Per-channel level meters, pan, mute and solo
- Master volume and level meter
- Selectable output sample rate (automatic by default; in shared mode the
  Windows sound settings take priority)
- WASAPI shared-mode output
- Tracks that end by themselves stop automatically
- Games and track lists are defined in XML files, so you can add or correct
  them yourself (see [docs/xml-format.md](docs/xml-format.md))

### What you need

- Windows 11 (64-bit). Chipestra has only been tested on Windows 11.
- .NET 10 Desktop Runtime (x64), available from
  [Microsoft](https://dotnet.microsoft.com/download/dotnet/10.0). If it is not
  installed, Windows shows a message with a download link when you start
  Chipestra.
- ROM files of the games you want to play. Chipestra does NOT include any
  ROMs, BIOS images or other game data. You must supply your own, and
  point Chipestra at the folder that contains them (Settings > ROM Root).

### Download and run

1. Download Chipestra-1.001.00.zip from the Releases page of this repository.
2. Extract it. You get a folder named Chipestra-1.001.00.
3. Run Chipestra.exe in that folder.
4. Open Settings and set ROM Root to the folder that contains your ROM files.
   Sub-folders are searched too.

The executable is not code-signed, so Windows SmartScreen may show a warning
on first launch (More info > Run anyway).

Settings are saved in %LOCALAPPDATA%\Chipestra\settings.json. To remove
Chipestra, delete the extracted folder and the %LOCALAPPDATA%\Chipestra folder.

### Building from source

Tools (the project is built and tested with these):

- Windows 11 (64-bit) and PowerShell 7 or later
- Visual Studio 2026 (18.x) with the workloads "Desktop development with C++"
  and ".NET desktop development", and the "vcpkg package manager" component
- .NET SDK 10 (the projects target net10.0 / net10.0-windows; built with 10.0.401)
- CMake 3.24 or later, on PATH (CMakeLists.txt requires 3.24; built with 4.4.2)
- vcpkg: the Native build uses vcpkg manifest mode
  (src/Chipestra.Native/vcpkg.json lists libsoxr, pinned by a baseline). The
  build script uses the vcpkg bundled with Visual Studio, or the one that
  VCPKG_ROOT points to. libsoxr is downloaded and built the first time you
  configure, so an internet connection is needed then.

Build and test everything (Release, x64) from a PowerShell prompt in the
repository root:

    .\tools\verify.ps1

This builds the Native library with CMake, runs the Native tests, builds
Chipestra.slnx with dotnet, runs the managed tests and checks that the build
output is consistent. The last check fails if the working tree has
uncommitted or untracked files. The application ends up in
src\Chipestra.App\bin\Release\net10.0-windows\Chipestra.exe.

Build the release package (run verify.ps1 first):

    .\tools\package-release.ps1 -Zip

This creates dist\Chipestra-<version>\ and dist\Chipestra-<version>.zip
(the version comes from version.props).

Tests that need ROM files are optional. Set the environment variable
CHIPESTRA_ROM_ROOT to a folder that contains your ROM files before running
verify.ps1. If it is not set, those tests are skipped.

### Contributing and support

Bug reports are welcome. Code contributions are not accepted for now.

Please do not ask where to get ROMs. Chipestra does not provide ROMs, and
questions about obtaining them will not be answered.

### License

Chipestra is free software, licensed under the GNU General Public License,
version 2 or (at your option) any later version. See [LICENSE](LICENSE).

Third-party code and libraries are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md); their license texts are in
the [licenses/](licenses/) folder. The source code of the bundled libsoxr
library (soxr.dll) is available as a separate download on the Releases page.

Copyright (C) 2026 Tom Water Hill

### Acknowledgments

Chipestra's CPU and sound-chip implementations are ported and adapted from
the work of many developers, especially the MAME project. See
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the full list of
copyright holders.

### Disclaimer

Chipestra is an independent project. It is not affiliated with or endorsed by
the MAME project or by any game publisher. All game titles and trademarks
belong to their respective owners.

## 日本語

Chipestra（チップエストラ。「チップ」と「オーケストラ」から）は、昔のアーケードゲームの音楽を Windows で再生するソフトです。録音した音を流すのではなく、ご自身で用意した ROM を使い、元の基板のサウンド CPU と音源チップをエミュレートして、ゲーム自身のサウンドプログラムを動かして鳴らします。家庭用ゲーム機への対応も予定しています。

Chipestra は作曲用のソフトではなく、VGM などの録音済みの音楽ファイルを再生するソフトでもありません。ROM は含まれていません。

バージョン: Ver 1.001.00

### 対応基板と確認済みタイトル

Chipestra は、対応している基板上のゲームを、XML 定義ファイルを書くことで追加できるように設計されています（[docs/xml-format.md](docs/xml-format.md) を参照。英語）。ここに挙げたタイトルは、作者が確認したものです。同じハードウェア系列でも、載っていないタイトルは、確認していません。

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
- Capcom 68000 Based: Street Fighter（ストリートファイター。US 版の ROM セット sf のみ対応。BGM、効果音、ボイスを再生できます）

### 機能

- チャンネルごとのレベルメーター、パン、ミュート、ソロ
- マスター音量とレベルメーター
- 出力サンプルレートの選択（既定は自動。共有モードでは、Windows のサウンド設定が優先されます）
- WASAPI 共有モードでの出力
- 自然に終わる曲は、終わると自動で停止
- ゲームと曲の一覧は XML ファイルで定義されているため、ご自身で追加や修正ができます（[docs/xml-format.md](docs/xml-format.md) を参照。英語）

### 必要なもの

- Windows 11（64bit）。Windows 11 でのみ動作を確認しています。
- .NET 10 Desktop Runtime（x64）。[Microsoft のサイト](https://dotnet.microsoft.com/download/dotnet/10.0)から入手できます。入っていない場合は、Chipestra の起動時に、Windows がダウンロード先の案内を表示します。
- 再生したいゲームの ROM ファイル。Chipestra には、ROM、BIOS イメージ、その他のゲームデータは一切含まれていません。ご自身で用意し、そのフォルダを Chipestra に指定してください（Settings > ROM Root）。

### ダウンロードと起動

1. このリポジトリの Releases のページから、Chipestra-1.001.00.zip をダウンロードします。
2. 展開すると、Chipestra-1.001.00 というフォルダができます。
3. そのフォルダの Chipestra.exe を起動します。
4. Settings を開き、ROM Root に、ROM ファイルのあるフォルダを指定します。サブフォルダの中も探します。

実行ファイルにはコード署名がないため、初回の起動で、Windows SmartScreen の警告が出ることがあります（「詳細情報」→「実行」）。

設定は %LOCALAPPDATA%\Chipestra\settings.json に保存されます。Chipestra を削除するときは、展開したフォルダと、%LOCALAPPDATA%\Chipestra フォルダを削除してください。

### ソースからのビルド

ツール（このプロジェクトは、次の環境でビルドと動作確認をしています）:

- Windows 11（64bit）と PowerShell 7 以降
- Visual Studio 2026（18.x）。ワークロード「C++ によるデスクトップ開発」と「.NET デスクトップ開発」、およびコンポーネント「vcpkg パッケージ マネージャー」
- .NET SDK 10（プロジェクトは net10.0 / net10.0-windows が対象。10.0.401 でビルド）
- CMake 3.24 以降（PATH に通す。CMakeLists.txt の要求は 3.24。4.4.2 でビルド）
- vcpkg: Native のビルドは vcpkg のマニフェストモードを使います（src/Chipestra.Native/vcpkg.json に libsoxr が書かれ、ベースラインで版が固定されています）。ビルド用のスクリプトは、Visual Studio に同梱の vcpkg、または VCPKG_ROOT が指す vcpkg を使います。libsoxr は、最初の構成（configure）のときにダウンロードしてビルドされるため、そのときはインターネット接続が必要です。

すべてのビルドとテスト（Release、x64）は、リポジトリの直下で、PowerShell から次のコマンドを実行します:

    .\tools\verify.ps1

Native ライブラリを CMake でビルドし、Native のテストを実行し、Chipestra.slnx を dotnet でビルドし、マネージドのテストを実行し、ビルド出力の整合を確認します。最後の確認は、作業ツリーにコミットされていないファイルや未追跡のファイルがあると失敗します。アプリケーションは src\Chipestra.App\bin\Release\net10.0-windows\Chipestra.exe にできます。

配布用のパッケージを作るには（先に verify.ps1 を実行します）:

    .\tools\package-release.ps1 -Zip

dist\Chipestra-<バージョン>\ と dist\Chipestra-<バージョン>.zip ができます（バージョンは version.props から決まります）。

ROM を使うテストは任意です。ROM ファイルのあるフォルダを、verify.ps1 を実行する前に、環境変数 CHIPESTRA_ROM_ROOT に設定してください。設定しない場合、それらのテストはスキップされます。

### 貢献とサポート

不具合の報告は、歓迎します。コードの貢献は、当面、受け付けていません。

ROM の入手方法は、質問しないでください。Chipestra は ROM を提供せず、入手方法の質問にも、お答えしません。

### ライセンス

Chipestra はフリーソフトウェアで、GNU General Public License バージョン 2、または（お好みで）それ以降のバージョンのもとで公開されています。[LICENSE](LICENSE) をご覧ください。

サードパーティのコードとライブラリは [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) に、ライセンス全文は [licenses/](licenses/) フォルダにあります。同梱している libsoxr ライブラリ（soxr.dll）のソースコードは、Releases のページで、別のファイルとして提供しています。

Copyright (C) 2026 Tom Water Hill

### 謝辞

Chipestra の CPU とサウンドチップの実装は、多くの開発者の仕事、特に MAME プロジェクトを参考に、移植・適応したものです。著作権者の一覧は [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) をご覧ください。

### 免責

Chipestra は独立したプロジェクトで、MAME プロジェクトやゲームの発売元とは関係がなく、承認も受けていません。ゲームのタイトルと商標は、それぞれの所有者に帰属します。
