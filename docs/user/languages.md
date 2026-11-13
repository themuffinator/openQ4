# Languages

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 can show its menus and game text in twelve languages. Choose one with **Settings → Game Options → General → Language** (in the classic menus, the Game Options page's **Gameplay** section). The menus switch at once; there is no need to restart.

| Language | `sys_lang` | What is translated |
|----------|------------|--------------------|
| English | `english` | Everything |
| French, Italian, Spanish | `french`, `italian`, `spanish` | Everything; voices with the retail language pack |
| German | `german` | Menus, settings, HUD and multiplayer messages, objectives, terminals, Arena Campaign and demos |
| Brazilian Portuguese | `brazilian` | The same as German |
| Czech | `czech` | The same as German |
| Hungarian | `hungarian` | The same as German |
| Turkish | `turkish` | The same as German |
| Ukrainian | `ukrainian` | The same as German |
| Polish, Russian | `polish`, `russian` | Menus and openQ4's own settings; other game text is in English |

On the very first launch openQ4 picks your system language if it is one of
these. Portuguese systems get Brazilian Portuguese. After that your choice is
kept. Game data from a GOG installer can include an `autoexec.cfg` in `q4base`
that names a language, often French when the installer was extracted on a Mac
or Linux. That file sets only the first launch's language; your choice in
Settings replaces it. You can also start in a given language from the command line:

```
openQ4-client_x64 +set sys_lang turkish
```

## Spoken dialogue

Character voices come from the retail Quake 4 language packs, not from openQ4.
If your copy includes the voice pack for the language you choose, characters
speak it; otherwise they speak English. Brazilian Portuguese, Czech, Hungarian,
Turkish and Ukrainian never had a voice pack, so they always use English voices.

## Fonts and typing

These languages use openQ4's scalable fonts, which have every letter they need.
For Czech, Hungarian, Turkish, Ukrainian and Russian, the setting that brings
back Quake 4's original bitmap fonts (`r_useTrueTypeFonts 0`) is ignored. Those
fonts lack the letters, and some would draw a different letter in their place.

Typing in chat, the console and name fields is still limited to Western
European letters. Cyrillic letters, and Czech, Hungarian or Turkish letters
outside that set (such as č, ő or ş), cannot be typed there yet.

## Reporting a translation problem

If a word is wrong or text does not fit its box, please open an issue on the
[openQ4 issue tracker](https://github.com/themuffinator/openQ4/issues). Include
the language, the screen and, if you can, a screenshot.
