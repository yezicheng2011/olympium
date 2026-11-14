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

## Optional homepage sentence

The homepage still says guest images are not published yet. That was true for the 0.1.0 package. Linux and Wine image releases now exist; Android and OpenHarmony images do not. Change that sentence only if you want the homepage to match the manual. Leave the 0.1.0 package and its SHA-256 as they are.
