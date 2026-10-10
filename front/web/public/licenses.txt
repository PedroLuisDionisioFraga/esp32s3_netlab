# Third-party material in the web UI

The UI code, the stylesheets and the design tokens are the project's own (MIT, see `../../LICENSE`). The pieces
below are not, and keep their own licences. The built UI carries a copy of this file as `licenses.txt`.

## Lucide icons (ISC)

The drawings in `src/ui/icons.ts` come from [Lucide](https://lucide.dev). Lucide is licensed under the ISC
License; portions derived from [Feather](https://feathericons.com) are under the MIT License.

```text
ISC License

Copyright (c) for portions of Lucide are held by Cole Bemis 2013-2022 as part of Feather (MIT).
All other copyright (c) for Lucide are held by Lucide Contributors 2022.

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL,
DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
OR PERFORMANCE OF THIS SOFTWARE.
```

The full text, including the Feather MIT notice, is at https://github.com/lucide-icons/lucide/blob/main/LICENSE.

## Fonts (SIL Open Font License 1.1)

- **Inter**: Copyright 2016 The Inter Project Authors (https://github.com/rsms/inter).
- **JetBrains Mono**: Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono).

Both are distributed through the npm packages `@fontsource-variable/inter` and
`@fontsource-variable/jetbrains-mono`, and used unmodified (Latin subset). The OFL 1.1 allows using, bundling and
redistributing the fonts with software, provided the copyright notice and the licence travel with them. The licence
text is in each package (`LICENSE`) and at https://openfontlicense.org.

## npm packages

`vue`, `vue-router` (MIT), `vite`, `vitest`, `typescript`, `vue-tsc` and `@vitejs/plugin-vue` (MIT, Apache-2.0 for
TypeScript) are build and runtime dependencies listed in `package.json`.
