# CustomBlurWindowLibrary - 要件定義書 (Draft)

- バージョン: 0.1-draft  
- 作成日: 2025-12-25  
- 作成者: Catharacta  
- ライセンス: MIT

---

## 1. 目的と背景
Windows 11 環境で、OS 提供の Acrylic/Mica より細かく制御できる「疑似透過（背景キャプチャ＋合成）」を行うカスタムブラーウィンドウ機能を提供する C++ ライブラリを作成する。アプリケーション開発者（特に Rust の windows-rs からの利用も想定）に対して、柔軟なブラー効果（複数実装を静的にバンドル）を簡単に利用できる API とデモを提供する。

主要ゴール:
- 高品質かつ低レイテンシで背景ブラーを表示するウィンドウをライブラリで提供する。
- ブラーの種類・パラメータをアプリから細かく制御可能にする。
- 将来の拡張（エフェクト追加・プラグイン）を見据えた設計にするが、当面は静的バンドルで実装する。

---

## 2. スコープ（含む / 含まない）
含む:
- Windows 11 対応（初版は Windows 11 のみ）
- C++ ライブラリ（ヘッダ + DLL/.lib）
- ブラー実装（初期）：Separable Gaussian（必須）、Dual/Kawase（推奨）、BoxBlur（オプション）
- キャプチャ抽象化（DXGI Desktop Duplication をデフォルト、Windows.Graphics.Capture を選択肢として実装）
- 表示: DirectComposition を推奨（UpdateLayeredWindow を互換パス）
- EffectPipeline（複数の Effect を直列に実行）
- C ABI ベースの薄い wrapper を提供（Rust からの利用を想定、簡易 example crate を同梱）
- デモアプリ: C++ サンプルアプリ + Tauri（Rust + Web UI）デモ（並行で簡易版を作成）
- ログ出力機能（有効/無効、ファイル出力やコールバック）
- JSON によるエフェクトチェーンの保存／読み込み

含まない（初版）:
- 動的プラグイン（DLL ロード）サポート（将来対応可能な設計にはする）
- 他プロセスのウインドウ自体の改変（非対応）
- 完全な CPU-only フォールバック（限定的な簡易モードのみ考慮）

---

## 3. 用語
- BlurWindow: 本ライブラリが作成・管理する疑似透過ウィンドウ（HWND）。
- Effect / IBlurEffect: ブラー等の画像処理モジュールの抽象単位。
- CaptureSubsystem: 画面領域を GPU テクスチャとして取得するモジュール。
- BlurManager / Composer: キャプチャ -> EffectPipeline -> 表示 を統括する管理モジュール。
- Preset / QualityPreset: 画質と性能の事前定義（High / Balanced / Performance / Minimal）。

---

## 4. 非機能要件（主要）
- 動作 OS: Windows 11 のみ（初版）。
- GPU 要件: Direct3D11 を必須とし、最低 Feature Level は `D3D_FEATURE_LEVEL_11_0` を想定（初版は 11_0 固定。将来 10_0 フォールバックを検討）。
- 目標 FPS: デフォルト 60 FPS を目標とする。負荷に応じて adaptiveQuality により 30 FPS に自動切替可能。
- レイテンシ: キャプチャ→表示の合計レイテンシ目安はプリセットで管理。推奨デフォルトの最大許容は 80ms。
- セキュリティ/プライバシー: キャプチャ機能はユーザにわかる形で説明する（ドキュメント）。DRM 保護領域は黒表示等の挙動を明記。
- ログ: デフォルト OFF。有効化すればファイル/コンソール/コールバックへ出力可能にする。

---

## 5. 機能要件（Functional Requirements）

### 5.1 ライフサイクル / 管理
- FR1: ライブラリ初期化・終了 API を提供する。
- FR2: BlurWindow の作成・破棄 API を提供する（owner HWND 指定、bounds・topMost・clickThrough 等）。
- FR3: ライブラリは複数の BlurWindow を同時に管理可能とする（リソース共有を行う）。

### 5.2 キャプチャ・表示
- FR4: CaptureSubsystem は指定領域をキャプチャし、GPU テクスチャ (ID3D11Texture2D) を BlurManager に渡す。
- FR5: 表示は DirectComposition をデフォルトで使用し、必要に応じ UpdateLayeredWindow をバックアップとして実装する。
- FR6: キャプチャ領域は BlurWindow のバウンディングボックスに基づく。マルチモニタ・DPI を考慮した座標変換を行う。

### 5.3 エフェクト / パイプライン
- FR7: EffectPipeline を構成できる（複数 IBlurEffect を順に実行）。
- FR8: 初期実装エフェクト:
  - Separable Gaussian（必須）: 横パス + 縦パス、sigma/radius 指定可能、自動サンプル数決定
  - Dual/Kawase（推奨）: iterations, offset
  - BoxBlur（オプション）
- FR9: エフェクトのパラメータはランタイムで変更でき、即時反映される。
- FR10: JSON による pipeline の保存とロードをサポートする。

### 5.4 Self-capture 回避（初期仕様）
- FR11: 初期の自己写り込み回避は「A: マスク＋キャッシュ」方式を採用する。  
  - 概要: キャプチャから自ウィンドウ領域を除外するのではなく、描画時に自ウィンドウ領域をマスクし、その領域は前フレームや縮小ブラーで補完する。移動時・重なり変化時は補正ロジック（フレーム差分）を適用して不自然さを低減する。

### 5.5 インターフェース／相互運用性
- FR12: C++ クラスベース API を提供する。
- FR13: Rust (windows-rs) から利用可能な C ABI の thin wrapper を提供する。簡易 Rust example crate を同梱する。
- FR14: ログ出力機能（有効/無効、パス指定、ユーザコールバック）を提供する。

---

## 6. API 概要（ハイレベル・擬似署名）

### C++（クラスベース・擬似）
```cpp
// ライフサイクル
bool BlurSystem::Initialize(const BlurSystemOptions& opts);
void BlurSystem::Shutdown();

// BlurWindow 操作
std::unique_ptr<BlurWindow> BlurSystem::CreateBlurWindow(HWND owner, const WindowOptions& opts);
class BlurWindow {
  void Start();
  void Stop();
  void SetEffectPipeline(const EffectPipelineDesc& pipeline);
  void SetPreset(QualityPreset preset);
  void SetClickThrough(bool enable);
  void SetTopMost(bool enable);
  void Destroy();
};
```

### C ABI（Rust 用 thin wrapper, 擬似）
```c
typedef void* BlurSystemHandle;
typedef void* BlurWindowHandle;

BlurSystemHandle blur_init(const BlurSystemOptions* opts);
void blur_shutdown(BlurSystemHandle sys);
BlurWindowHandle blur_create_window(BlurSystemHandle sys, HWND owner, const WindowOptions* opts);
int blur_start_window(BlurWindowHandle w);
int blur_stop_window(BlurWindowHandle w);
int blur_set_pipeline(BlurWindowHandle w, const char* json_pipeline);
int blur_set_preset(BlurWindowHandle w, int presetId);
void blur_enable_logging(BlurSystemHandle sys, bool enable, const char* path);
```

> 実装ではエラーコードや非同期処理の扱いを明確に定義すること（戻り値やコールバック）。

---

## 7. 設定ファイル（JSON）スキーマ（概要）
トップレベル例:
```json
{
  "windowOptions": {
    "topMost": true,
    "clickThrough": true,
    "bounds": { "x": 100, "y": 100, "w": 800, "h": 600 }
  },
  "pipeline": [
    { "name": "Gaussian", "params": { "sigma": 6.0, "downscale": 0.5 } },
    { "name": "Kawase",  "params": { "iterations": 2, "offset": 3.0 } }
  ],
  "preset": "Balanced",
  "logging": { "enabled": false, "path": null }
}
```
- 各 effect の params は effect 固有のスキーマを持つ（ドキュメント化する）。

---

## 8. 品質基準と受け入れ基準（Acceptance Criteria）
- AC1: Balanced プリセットで 1920×1080 のウィンドウが（代表的統合 GPU 上で）滑らかに見えること。理想は 60FPS、実環境で下がる場合は adaptiveQuality により 30FPS へ自動移行すること。
- AC2: Performance プリセットで低性能 GPU 環境にて 30FPS を下回らないこと（±許容）。
- AC3: API で blur radius / preset / pipeline を変更すると表示が即時に反映される。
- AC4: JSON 保存・読み込みで pipeline が再現可能である。
- AC5: ログ出力が有効になった場合、指定されたパスへログが出力される。
- AC6: Self-capture 回避（マスク＋キャッシュ）により、通常の使用で自ウィンドウの自己写り込みが顕著に見えないこと（移動時に重大なちらつき・点滅を発生させない）。

---

## 9. テスト計画（概要）
- 単体テスト: API の境界条件、エラー経路、パラメータの境界チェック。
- 統合テスト: Capture -> Pipeline -> Presentation の end-to-end テスト。
- パフォーマンステスト: 実FPS, GPU 時間, CPU 時間, レイテンシを計測。QualityPreset ごとに測定。
- 互換性テスト: 複数モニタ、DPIスケール、ウィンドウ移動/リサイズ、複数ウィンドウ同時動作。
- 受け入れテスト: 前述の AC を満たすか手動/自動で検証。
- テスト機材候補（例）:
  - High: Discrete GPU（NVIDIA/AMD）
  - Mid: 現行世代統合 GPU（Intel Iris Xe / AMD Ryzen integrated）
  - Low: 古めの統合GPU相当（性能低めのノート）  
  → ※ユーザは Intel Iris Xe をハイスペック過ぎると指摘しているため、代表的な中堅統合 GPU を含む複数構成で測定する。

---

## 10. リスクと軽減策
- R1: 自ウィンドウの自己写り込み（artifact）
  - 対策: 初期は「マスク＋キャッシュ」を採用し、移動期間のみ補正ロジックを強化。将来、ウィンドウ別合成をオプションで追加する。
- R2: DRM 保護領域が黒またはキャプチャ不可
  - 対策: ドキュメントと UI でユーザに通知。
- R3: 低性能 GPU でのパフォーマンス不足
  - 対策: QualityPreset と adaptiveQuality、BoxBlur 等の軽量モードを提供。
- R4: FFI（Rust）での安全性問題
  - 対策: C ABI を明示的に設計し、Rust example crate を同梱して推奨のバインディングを提供。
- R5: DirectComposition の互換性や DWM の挙動差異
  - 対策: UpdateLayeredWindow の互換コードと十分なテストを用意。

---

## 11. 開発ロードマップ（マイルストーン & 概算工数）

概算合計: 10–12 週間（目安）

- M0 準備 (1 週)
  - 詳細設計確定、開発環境（CMake/CI/Windows SDK）整備、Device 要件チェックコード作成

- M1 PoC (2 週)
  - DXGI Desktop Duplication でキャプチャ → D3D11 テクスチャ取得
  - Separable Gaussian（2 パス）実装
  - 表示: UpdateLayeredWindow 経由の簡易表示
  - Self-capture: PoC 用に一時非表示フローも実装して比較検証

- M2 コア (3 週)
  - DirectComposition 統合
  - EffectPipeline と ResourceManager の実装
  - C++ クラス API 定義と実装（ヘッダ）
  - C ABI thin wrapper インターフェース設計

- M3 機能追加 (2 週)
  - Dual/Kawase 実装、BoxBlur 実装
  - JSON 設定入出力
  - Self-capture: マスク＋キャッシュ実装（既定）

- M4 デモ & テスト (2 週)
  - C++ デモアプリ（UI）実装
  - Tauri デモ（簡易）実装（並行）
  - Rust example crate 作成
  - 基本的な自動テスト・パフォーマンス測定スクリプト

- M5 ドキュメント & リリース (1 週)
  - API リファレンス、導入ガイド、サンプル README
  - GitHub Release、バイナリパッケージ

備考: 実作業は並列化可能（シェーダ開発、Capture 実装、API 実装は独立タスク）。実機テストが早期に必要。

---

## 12. 配布・ライセンス・メンテナンス
- ライセンス: MIT（指定）
- 配布: GitHub（ソース + Releases バイナリ）。将来 vcpkg などパッケージ化を検討。
- バージョニング: SemVer を採用。初期は 0.x 開発フェーズ。
- ドキュメント: README (Quick Start)、API Reference、チュートリアル（C++ と Rust）、FAQ（DRM・権限関連）。

---

## 13. ドキュメントに含める作業成果（最終リリースに向けて）
- API リファレンス（C++ ヘッダ + C ABI 仕様）
- Rust example crate（bindings + 使用例）
- C++ デモアプリのソース
- Tauri デモ（簡易）ソース
- JSON pipeline のサンプル
- パフォーマンス測定レポート（代表的 GPU/設定）
- Known Issues と Workarounds（DRM, 古い GPU など）

---

## 14. 受け入れテストケース（代表）
- TC1: 起動時に D3D11 device を生成できること（Feature Level チェック）。
- TC2: BlurWindow の作成・表示・破棄が正常に動作すること（複数ウィンドウ同時）。
- TC3: Balanced プリセットでの FPS 測定（記録）。
- TC4: API で pipeline を動的に切替しても表示破綻がないこと。
- TC5: JSON で保存→読み込んだ pipeline が同等の見た目を再現すること。
- TC6: Self-capture 回避（マスク＋キャッシュ）で通常の操作（移動・リサイズ）時に重大なちらつきがないこと。
- TC7: ログ有効化により指定ファイルへログが出力されること。
- TC8: Rust example で基本的な Create/Start/Stop/SetPipeline 操作が可能なこと。

---

## 15. 未確定/今後検討する事項
- 将来的にプラグイン（DLL）をサポートするか、その場合の ABI/バージョンポリシー（現時点では実装しないが設計に留意）。
- Windows.Graphics.Capture (WinRT) をデフォルトにするか、DXGI をデフォルトにするか（現状: DXGI をデフォルトで実装、必要なら WinRT 経由を追加）。
- より詳しい QA 測定環境（最低限サポートする具体的 GPU 機種リスト）はプロジェクト開始時に実機で決定する。

---

## 16. 次のアクション（提案）
1. 本要件定義書のレビューと承認（このドキュメントに修正があれば指示ください）。  
2. 承認後、M0（詳細設計・環境準備）に着手。  
3. 併せて簡易の UI ワイヤーフレーム（C++ デモの画面設計、Tauri デモの画面構成）を作成。  
4. PoC（M1）にて最短で動くパイプライン（DXGI + D3D11 + separable Gaussian + UpdateLayeredWindow）を実装し、動作確認を行う。

---

必要であれば、この要件定義書を次の形式で出力できます:
- Markdown（このまま）
- PDF（Markdown を変換）
- 追加で UML のクラス図 / シーケンス図（PlantUML 形式 または 画像）

ご確認ください。修正・追記したい箇所があれば具体的に指示してください。