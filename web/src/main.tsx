import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'

// ホーム画面に追加せずChromeのタブで直接開いた場合、ページ読み込み時点では
// ブラウザの制約上フルスクリーンAPIを自動起動できない（ユーザー操作が必須）。
// そのため最初のタップ操作を検知した瞬間にフルスクリーン化する。
function enterFullscreenOnFirstTap() {
  document.removeEventListener('pointerdown', enterFullscreenOnFirstTap)
  if (!document.fullscreenElement) {
    document.documentElement.requestFullscreen?.().catch(() => {})
  }
}
document.addEventListener('pointerdown', enterFullscreenOnFirstTap, { once: true })

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
