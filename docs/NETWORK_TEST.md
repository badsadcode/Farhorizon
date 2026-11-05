# Trying co-op

There are two ways: the server program, which needs nobody at the keyboard, or one player hosting from the game.
Joining is by command line for now.

## With the server program (your friend's PC)
Build the zip with `python tools/package_server.py` (it lands in `dist/`) and send it. Your friend unzips it, runs `farhorizon_server.exe`, fills in a password and presses **Start server**. The **Help** tab walks through the router.

Forward one **UDP** port on the router to that PC (27777 unless you change it), and allow the program through
Windows Firewall. Players then join with the PC's public address:

```
farhorizon --join 203.0.113.7 --password yourword --name Mara
```

The first person aboard gets the helm. The world holds still while nobody is connected and is saved in
`saves/server_world.json` when the server stops. To start a new world, stop the server and delete that file.

There is no encryption of the game itself. The password is never sent as readable text, but it is stored as
readable text in `server_config.json` on the server. Use a password you don't use for anything else.

## On one PC
Open two terminals in the folder with `farhorizon.exe`. Give the second its own settings file:

```
farhorizon --host --name Tomek --password test
farhorizon --join 127.0.0.1 --name Mara --password test --settings settings2.json
```

## On a home network
The host's address is shown by `ipconfig` as the IPv4 address, for example `192.168.1.20`.

```
farhorizon --host --name Tomek --password test
farhorizon --join 192.168.1.20 --name Mara --password test
```

Windows Firewall asks about the game the first time it hosts. Allow it on private networks. Both players need
the same version of the game.

## What to try
* The host flies with the usual keys. The other player presses F to leave their seat and walks the decks.
* You should see each other, with names overhead, and the planets should move past the windows for both.
* If the host leaves the seat, the other player can walk to the seat and take the helm.
* When docked, the pilot presses L to undock. Everyone must be seated first.
