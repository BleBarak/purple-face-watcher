#!/usr/bin/env python3
"""Play the Wi-Fi games against the Watcher from a computer, with the Purple
app's messages (see docs/friends-protocol.md).

    python3 tools/test_friends.py --ip=<watcher ip> [--start=rps|dice|coin]

--ip: the Watcher's address (shown in your router, or in its serial log).
--start: start that game yourself; without it, wait for Joy to start one.
Joy plays with one friend at a time, so close the Purple app first."""
import json, random, socket, sys, time

args = dict(a.lstrip('-').split('=', 1) for a in sys.argv[1:] if '=' in a)
ip, start = args['ip'], args.get('start')
BEAT = 0.9

def coin(r): return 'Heads' if ((r * 2654435761) >> 7) % 2 == 0 else 'Tails'
def pick(game, r):
    if game == 'rps': return random.choice(['rock', 'paper', 'scissors'])
    if game == 'dice': return str(random.randint(1, 6))
    g = random.choice(['Heads', 'Tails']); return f'{g}|{1 if g == coin(r) else 0}'
def compare(game, a, b):
    if game == 'rps':
        i = ['rock', 'paper', 'scissors']; x, y = i.index(a), i.index(b)
        return 0 if x == y else (1 if (x - y) % 3 == 1 else -1)
    if game == 'dice': return (int(a) > int(b)) - (int(a) < int(b))
    return (int(a[-1]) > int(b[-1])) - (int(a[-1]) < int(b[-1]))
def delay(game): return {'rps': 3, 'dice': 2, 'coin': 2.5}[game] * BEAT

s = socket.create_connection((ip, 47811), timeout=5)
f = s.makefile('rw')
def send(m): f.write(json.dumps(m) + '\n'); f.flush(); print('  ->', m)
send({'t': 'lan', 'name': 'Computer#ffffff'})
send({'t': 'hello', 'name': 'Computer', 'token': 'ffffff'})
game, mine, wins = None, {}, [0, 0]
s.settimeout(60)
t0 = time.time()
while time.time() - t0 < 90:
    m = json.loads(f.readline()); 
    if m['t'] != 'ping': print('<-', m)
    if m['t'] == 'hello' and 'acts' in m: print('   Joy can play:', m['acts'])
    if m['t'] == 'hello' and start:
        game = start; time.sleep(1); send({'t': 'start', 'a': game})
        send({'t': 'act', 'a': game, 'k': 'round', 'r': 1}); time.sleep(delay(game))
        mine[1] = pick(game, 1); send({'t': 'act', 'a': game, 'k': 'pick', 'r': 1, 'p': mine[1]}); r = 1
    if m['t'] == 'start': game = m['a']
    if m['t'] == 'act' and m['k'] == 'round':
        r = m['r']; time.sleep(delay(game)); mine[r] = pick(game, r)
        send({'t': 'act', 'a': game, 'k': 'pick', 'r': r, 'p': mine[r]})
    if m['t'] == 'act' and m['k'] == 'pick':
        r = m['r']; out = compare(game, mine[r], m['p'])
        wins[0] += out > 0; wins[1] += out < 0
        extra = f' (coin was {coin(r)})' if game == 'coin' else ''
        print(f'   {game} round {r}: computer {mine[r]} vs Joy {m["p"]}{extra} -> {wins[0]}-{wins[1]}')
        if max(wins) >= 2: print('GAME OVER:', 'computer wins' if wins[0] > wins[1] else 'Joy wins'); break
        if start:
            time.sleep(BEAT * 2.5); r += 1; send({'t': 'act', 'a': game, 'k': 'round', 'r': r})
            time.sleep(delay(game)); mine[r] = pick(game, r); send({'t': 'act', 'a': game, 'k': 'pick', 'r': r, 'p': mine[r]})
