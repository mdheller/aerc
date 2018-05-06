# aerc

aerc is a **work in progress** asynchronous email client for your terminal. aerc
is network-first, and is designed with network-based email protocols in mind. It
runs all network code in separate worker threads that don't lock up the UI.
Compared to mutt, it's also easier on the network and much faster - it only
fetches what it needs.

[Join the IRC channel](http://webchat.freenode.net/?channels=aerc&uio=d4) (#aerc on irc.freenode.net).

<p align="center">
    <img src="https://sr.ht/Klj3.png" />
</p>

If you'd like to support aerc development, you can contribute to [my Patreon
page](https://patreon.com/sircmpwn).

## Status

[aerc status](https://github.com/SirCmpwn/aerc/issues/72)

## Features

Note: aerc is not done, some of these are planned or in-progress

* Vim-style keybindings and commands
* Custom pipelines for email handling (highlighting diffs, rendering HTML, etc)
* Integrated tools for patch review, git, mailing lists, etc
* View or compose emails with arbitrary tools in the embedded terminal
* Integrated address book and tab completion for contacts
* Support for multiple accounts with different backends
* Out of the box PGP support with Keybase integration

## Compiling from Source

Install dependencies:

* libtsm
* termbox
* openssl (optional, for SSL support)
* cmocka (optional, for tests)

Run these commands:

```shell
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc ..
make
sudo make install
```

Copy config/* to ~/.config/aerc/ and edit them to your liking.
