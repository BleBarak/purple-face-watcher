# Wi-Fi games: the messages

How the Watcher ("Joy") and the Purple app find each other and play. Handy if
you want to make your own friend (another board, a PC, a robot).
`tools/test_friends.py` is a working example in Python.

## Finding each other

Every 2 seconds each side broadcasts one UDP packet to port **47810**
(`255.255.255.255`):

```json
{"purple": 1, "name": "Joy#000000", "port": 47811, "client": false}
```

- `name` is the display name, `#`, and a 6-character token
- `port` is the TCP port it listens on
- `client: false` means "I don't connect out, you connect to me" (the
  Watcher). Between two phones, the one whose name sorts first connects.

## Talking

TCP to the announced port, **one JSON object per line** (`\n` at the end).
The side that connected first sends who it is:

```json
{"t": "lan", "name": "Purple#3fa9c1"}
```

Then both sides send `hello`:

```json
{"t": "hello", "name": "Joy", "token": "000000", "accent": 4288120559, "acts": ["rps", "dice", "coin"]}
```

- `token`: the side with the smaller token **leads** (starts games on its
  own). The Watcher uses `000000`, so it always leads.
- `accent`: the face colour as a 32-bit ARGB number
- `acts`: the activities this side knows. The app only offers and starts
  those. Leave it out to mean "everything".

Every 10 s each side sends `{"t": "ping"}`. Nothing heard for 25 s means the
friend is gone.

## Games

The three "rounds" games share one shape: best of three (first to 2 wins,
at most 7 rounds), ties replay. The side that starts the game starts every
round.

```json
{"t": "start", "a": "dice"}
{"t": "act", "a": "dice", "k": "round", "r": 1}
{"t": "act", "a": "dice", "k": "pick", "r": 1, "p": "5"}
```

Both sides pick after a short show (a 0.9 s "beat"), send their pick, and
work out the result the same way:

| `a` | wait before picking | `p` | wins |
|---|---|---|---|
| `rps` | 3 beats (3-2-1) | `rock`, `paper`, `scissors` | the usual |
| `dice` | 2 beats (rolling) | `"1"` to `"6"` | higher |
| `coin` | 2.5 beats | `"Heads\|1"` = guessed Heads, right (`"Tails\|0"` = Tails, wrong) | right beats wrong |

For the coin, both sides know the coin without sending it:
`coin(r) = ((r * 2654435761) >> 7) % 2 == 0 ? "Heads" : "Tails"`.

After a round both wait 2.5 beats; then the starter sends the next `round`, or
both end the game when someone has 2 wins.
