# Privacy

Vectrex_EMU is a free, open-source program. It has no accounts, no ads, no analytics and no tracking. Your ROMs,
pictures, settings, saves and recordings stay on your device. The program only talks to the internet in the cases
below, and only for what is described there.

## Update check

At start the PC program asks GitHub for the newest release's name
(`https://api.github.com/repos/hugonl31/Vectrex_EMU/releases/latest`) to tell you when a new version is out. Nothing
about you is sent; GitHub sees the request as any web request (your IP address), under
[GitHub's privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).
It can be switched off in the settings.

## Online play

Online play uses a small meeting-point server run by the author (`tools/vxrelay.py` in this repository):

* **To connect two players** the server passes each player's network address (IP and port) to the other player of
  that game, and may relay the game's data when a direct connection is not possible. Addresses are used only while
  the game is being set up or played and are not stored.
* **Public games** are listed in the game room with the host's player name and the game's name until someone joins.
* **Player names**: a name you choose is reserved for you. The server stores the name, a one-way hash (SHA-256) of a
  random key kept in your settings, and when the name was last used - no address. A name unused for 365 days is
  removed.
* **Chat** messages go between the two players and are not stored by the server.

## Debug reports

Only when you choose **Help > Debug report > Send**, a report is sent to the author's server: the program's version,
your system and graphics card, performance figures, the program's log and a copy of the settings - no ROMs, no files,
no online key or player name. If you fill in the optional email field, that address goes with the report so the author
can tell you when a fix is out. Reports are kept for fixing problems only, never shared, and the oldest are deleted
automatically. The sender's IP address is used only in memory to limit reports per day and is never written.
**Save as file** keeps the report on your device and sends nothing.

## Phone apps and RetroArch

The iPhone / iPad and Android apps and the RetroArch core follow the same rules: online play and debug reports as
above, nothing else sent. Test builds given privately to testers may send detailed diagnostic logs to the author's
server; the public versions do not.

## Contact

Questions about privacy: open an issue at https://github.com/hugonl31/Vectrex_EMU/issues.
