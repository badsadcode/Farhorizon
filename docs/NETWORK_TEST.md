# Trying co-op

For now this runs from a command line. A menu and a separate server program come with roadmap step 4.4.

## On one PC
Open two terminals in the folder with `farhorizon.exe`. Give the second its own settings file:

```
farhorizon --host --name Tomek
farhorizon --join 127.0.0.1 --name Mara --settings settings2.json
```

## On a home network
The host's address is shown by `ipconfig` as the IPv4 address, for example `192.168.1.20`.

```
farhorizon --host --name Tomek
farhorizon --join 192.168.1.20 --name Mara
```

Windows Firewall asks about the game the first time it hosts. Allow it on private networks. Both players need
the same version of the game.

## Over the internet
Not ready yet: there is no password. Please wait for step 4.4 before opening a port on a router.

## What to try
* The host flies with the usual keys. The other player presses F to leave their seat and walks the decks.
* You should see each other, with names overhead, and the planets should move past the windows for both.
* If the host leaves the seat, the other player can walk to the seat and take the helm.
* When docked, the pilot presses L to undock. Everyone must be seated first.
