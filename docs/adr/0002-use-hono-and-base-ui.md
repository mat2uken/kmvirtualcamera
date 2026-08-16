# ADR 0002: Cloudflare Worker における Hono および Web ページにおける Base UI の採用

## ステータス
承認済 (Accepted)

## 背景 (Context)
シグナリングサーバー（Cloudflare Worker）のルーティング・バリデーション・エラーハンドリングの型安全性および保守性を高め、送信側Webページ（`/send/`）のUI品質・アクセシビリティ・UXを高める必要があります。

## 決定事項 (Decision)
1. **Cloudflare Worker 側フレームワーク**:
   - **`Hono`** を採用する。
   - `Hono` は Cloudflare Workers に最適化された超軽量・高速なWebフレームワークであり、URLルーティング、ミドルウェア（Request ID、セキュリティヘッダー、エラーハンドリング）、型安全なコンテキスト、Durable Objects との統合をエレガントに実現できる。
2. **送信側 Web ページ UI**:
   - **`Base UI`**（MUIチームによる非装飾・アクセシブルな高品質コンポーネントライブラリ）を採用する。
   - React + Vite + Base UI により、カメラ/マイク選択、ローカルプレビュー、ステータスバッジ、折りたたみ式診断パネルをモダンかつアクセシブルに構築する。
   - 静的アセット（HTML/JS/CSS）としてビルドし、Cloudflare Worker Static Assets 経由で同一オリジン（`/send/`）から配信する。

## 影響 (Consequences)
- **メリット**:
  - API エンドポイントの定義と OpenAPI スキーマの整合性が高まり、コードが簡潔になる。
  - Web 送信ページの UI/UX とアクセシビリティが向上し、モバイル/デスクトップでの操作性が大幅に改善される。
  - Worker Static Assets により、単一のデプロイ単位を維持できる。
