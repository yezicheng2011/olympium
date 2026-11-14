# Putting the manual on olympium.net

The live site is on AX41 at `/var/www-de/olympium.net`. This repository cannot deploy there. After review, copy the manual into that tree and add a navigation link on the existing homepage.

## Files to copy

Copy `site/manual/index.html` to:

```text
/var/www-de/olympium.net/manual/index.html
```

The page loads fonts, the favicon, and the mark from the paths the homepage already uses (`/fonts/`, `/favicon.svg`). Do not upload a second copy of those files. Do not replace `/var/www-de/olympium.net/download/v0.1.0/Linkspan-0.1.0.pkg`.

The public package URL stays:

```text
https://olympium.net/download/v0.1.0/Linkspan-0.1.0.pkg
https://github.com/yezicheng2011/olympium/releases/download/v0.1.0/Linkspan-0.1.0.pkg
```

## Navigation

In the homepage header list, add:

```html
<li><a href="/manual/">Manual</a></li>
```

Add the same item under the footer Product list. The header list is hidden below 860px, so also add a Manual link in `.nav .right`, before the GitHub button, using the same button classes the homepage already uses. The manual page itself includes that link.

## Rollback

Before replacing the live manual, copy it aside:

```text
cp -a /var/www-de/olympium.net/manual/index.html /var/www-de/olympium.net/manual/index.html.bak
```

To roll back, move that copy back over `index.html`. Do not delete or replace `/var/www-de/olympium.net/download/v0.1.0/Linkspan-0.1.0.pkg`. The 0.1.1 package built by CI is a test artifact. It is not this URL.

## Homepage sentence

Replace a sentence that says no guest images are published. Use this, and do not change the 0.1.0 download link:

```html
<p>The published Mac download is still Linkspan 0.1.0. Its built-in list has no guest checksums. A newer client can fetch the published Linux and Wine archives from GitHub, or from the China mirror when the client IP is in CN. Android and OpenHarmony images are not published. On a Mac, install QEMU yourself before booting the Linux guest. The package does not include QEMU.</p>
```

## Support link

The tips page is a separate bundle. Add `<a href="/support/">Support</a>` without changing the download or manual href. Leave wlsaplus cache timers disabled and leave its downloads unchanged.

## What the manual is allowed to claim

Linux slim 0.1.1 and Wine 0.1.0 have published archives and recorded checksums. Android and OpenHarmony do not. Wine is a Linux x86_64 program and does not run on a Mac. The old Linkspan 0.1.0 package URL stays in place.
