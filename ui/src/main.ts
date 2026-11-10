import { mount } from 'svelte';
import App from './App.svelte';
import './app.css';

// Kept for support: the shell can read these through the pagedump test hook,
// and a rendering failure is otherwise invisible inside WKWebView.
const errors: string[] = [];
(window as unknown as { __lioraErrors?: string[] }).__lioraErrors = errors;
window.addEventListener('error', (event) => {
  errors.push(String(event.message || event.error || 'error'));
});
window.addEventListener('unhandledrejection', (event) => {
  errors.push(String(event.reason ?? 'rejection'));
});

const app = mount(App, { target: document.getElementById('app')! });

export default app;
