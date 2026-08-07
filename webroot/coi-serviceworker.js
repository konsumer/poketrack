// Enables cross-origin isolation (COOP/COEP) on hosts that can't set custom
// response headers, e.g. GitHub Pages. Cross-origin isolation is required by
// browsers before `new SharedArrayBuffer()` / `new WebAssembly.Memory({shared:true})`
// is allowed — which WCLAP plugins declaring shared/thread-capable memory need
// (see wclap_host_web.lib.js). Without this, such plugins fail to load with a
// clear console error instead of hanging, but can never actually use threads.
//
// Registers a service worker that re-serves same-origin requests with COOP/COEP
// headers attached. The first load after registration reloads once so the
// top-level document itself picks up the headers.
if (typeof window !== 'undefined') {
  if (window.crossOriginIsolated !== true && 'serviceWorker' in navigator) {
    navigator.serviceWorker.register(document.currentScript.src).then((registration) => {
      registration.addEventListener('updatefound', () => {
        window.location.reload()
      })
      // Reload once we have an active controller so this navigation is re-served with headers.
      if (registration.active && !navigator.serviceWorker.controller) {
        window.location.reload()
      }
    }).catch((e) => {
      console.error('coi-serviceworker: registration failed, plugins needing shared memory/threads will not load:', e)
    })
  }
} else {
  // Running as the actual service worker.
  self.addEventListener('install', () => self.skipWaiting())
  self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()))

  self.addEventListener('fetch', (e) => {
    const request = e.request
    if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return
    e.respondWith(
      fetch(request).then((response) => {
        if (response.status !== 0 && !response.headers.get('Cross-Origin-Embedder-Policy')) {
          const headers = new Headers(response.headers)
          headers.set('Cross-Origin-Embedder-Policy', 'require-corp')
          headers.set('Cross-Origin-Opener-Policy', 'same-origin')
          return new Response(response.body, { status: response.status, statusText: response.statusText, headers })
        }
        return response
      }).catch((e) => console.error('coi-serviceworker: fetch failed', e))
    )
  })
}
