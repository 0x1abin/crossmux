<header class="brand">
  <div class="brand-inner">
    <h1>
      <a class="brand-link" href="/"
        ><img
          class="brand-logo"
          src="/assets/crosspoint-icon.svg"
          alt=""
          width="32"
          height="32"
        />CrossMux</a
      >
    </h1>
    <nav class="nav-links desktop-nav">
      <a href="/">主页</a>
      <a href="/files">文件</a>
      <a href="/settings">设置</a>
      <a href="/fonts">字体</a>
    </nav>
  </div>
  <details class="mobile-nav">
    <summary aria-label="导航菜单">
      <svg class="icon menu-open" viewBox="0 0 24 24">
        <path d="M4 7h16M4 12h16M4 17h16" /></svg
      ><svg class="icon menu-close" viewBox="0 0 24 24">
        <path d="m6 6 12 12M6 18 18 6" />
      </svg>
    </summary>
    <nav class="nav-links mobile-links">
      <a href="/">主页</a>
      <a href="/files">文件</a>
      <a href="/settings">设置</a>
      <a href="/fonts">字体</a>
    </nav>
  </details>
</header>
<script>
  document.querySelectorAll('.brand .nav-links a').forEach(link => {
    if (link.pathname === location.pathname) {
      link.classList.add('active');
      link.setAttribute('aria-current', 'page');
    }
  });
</script>
