<footer class="site-footer">
  <div class="footer-inner">
    <div class="footer-main">
      <div class="footer-brand"><a class="brand-link" href="/"><img class="brand-logo" src="/assets/crosspoint-icon.svg" alt="" width="28" height="28" />CrossMux</a><p>社区维护的阅读固件，支持阅读与轻量应用。</p></div>
      <nav class="footer-project"><span class="footer-label">项目</span><a href="https://github.com/0x1abin/crossmux/releases" target="_blank" rel="noopener noreferrer">固件发布</a><a href="https://github.com/0x1abin/crossmux" target="_blank" rel="noopener noreferrer">GitHub</a></nav>
    </div>
    <div class="footer-bottom"><span>CrossMux 基于 MIT 许可证开源。</span><span>由社区构建，为社区服务。</span></div>
  </div>
</footer>
<dialog id="webMessageDialog" class="web-dialog" aria-labelledby="webDialogTitle" aria-describedby="webDialogMessage">
  <form method="dialog">
    <h2 id="webDialogTitle"></h2>
    <p id="webDialogMessage"></p>
    <div class="web-dialog-actions">
      <button id="webDialogCancel" value="cancel">取消</button>
      <button id="webDialogConfirm" value="confirm">确定</button>
    </div>
  </form>
</dialog>
<div id="webToast" class="toast" role="status" aria-live="polite"></div>
<script>
const webMessageDialog = document.getElementById('webMessageDialog');
const webDialogTitle = document.getElementById('webDialogTitle');
const webDialogMessage = document.getElementById('webDialogMessage');
const webDialogCancel = document.getElementById('webDialogCancel');
const webDialogConfirm = document.getElementById('webDialogConfirm');
let webDialogQueue = Promise.resolve();

function showWebDialog(title, message, confirm) {
  const result = webDialogQueue.then(() => new Promise(resolve => {
    webDialogTitle.textContent = title;
    webDialogMessage.textContent = String(message);
    webDialogCancel.hidden = !confirm;
    webDialogConfirm.textContent = confirm ? '确认' : '确定';
    webMessageDialog.returnValue = '';
    webMessageDialog.addEventListener('close', () => {
      resolve(webMessageDialog.returnValue === 'confirm');
    }, { once: true });
    webMessageDialog.showModal();
  }));
  webDialogQueue = result.then(() => undefined, () => undefined);
  return result;
}

function showWebAlert(message) {
  return showWebDialog('提示', message, false).then(() => undefined);
}

function showWebConfirm(message) {
  return showWebDialog('确认操作', message, true);
}

const webToast = document.getElementById('webToast');
let webToastTimeout;

function showWebToast(message, isError) {
  clearTimeout(webToastTimeout);
  webToast.textContent = message;
  webToast.className = 'toast' + (isError ? ' error' : '');
  webToast.style.display = 'block';
  webToastTimeout = setTimeout(() => { webToast.style.display = 'none'; }, 4000);
}
</script>
