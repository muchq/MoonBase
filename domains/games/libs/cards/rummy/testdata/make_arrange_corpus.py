# An independent brute force: the corpus both searches (C++ and TS) replay.
import itertools, json, random
RANKS=['A','2','3','4','5','6','7','8','9','10','J','Q','K']
SUITS=['♣','♦','♥','♠']
DECK=[r+s for s in SUITS for r in RANKS]
def rank(c): return c[:-1]
def suit(c): return c[-1]
def pts(c):
    i=RANKS.index(rank(c)); return 10 if i>=10 else i+1
def is_meld(cs):
    if len(cs)<3 or len(set(cs))!=len(cs): return False
    if all(rank(c)==rank(cs[0]) for c in cs): return len(cs)<=4
    if not all(suit(c)==suit(cs[0]) for c in cs): return False
    for hi in (False,True):
        v=sorted((13 if hi and rank(c)=='A' else RANKS.index(rank(c))) for c in cs)
        if all(v[i]==v[i-1]+1 for i in range(1,len(v))): return True
    return False
def partitionable(cs):
    if not cs: return True
    first,rest=cs[0],cs[1:]
    for k in range(2,len(rest)+1):
        for combo in itertools.combinations(rest,k):
            g=[first,*combo]
            if is_meld(g) and partitionable([c for c in rest if c not in combo]): return True
    return False
def best(hand):
    b=sum(map(pts,hand))
    for k in range(3,len(hand)+1):
        for m in itertools.combinations(hand,k):
            d=sum(pts(c) for c in hand if c not in m)
            if d<b and partitionable(list(m)): b=d
    return b
def lays(cards,onto):
    # each card to one meld; every grown meld valid
    for assign in itertools.product(range(len(onto)),repeat=len(cards)):
        groups=[list(m) for m in onto]
        for c,i in zip(cards,assign): groups[i].append(c)
        if all(is_meld(g) for g in groups): return True
    return False
def best_layoff(hand,onto):
    cand=[c for c in hand if any(rank(c)==rank(m[0]) or suit(c)==suit(m[0]) for m in onto)]
    b=best(hand)
    for k in range(1,len(cand)+1):
        for L in itertools.combinations(cand,k):
            if lays(list(L),onto):
                d=best([c for c in hand if c not in L])
                if d<b: b=d
    return b
H=lambda s:s.split()
curated=[
 'A♥ K♥ Q♥ J♥ 10♥ A♣ K♣ Q♣ J♣ 10♣',      # two ace-high runs: gin
 'K♠ A♠ 2♠ 3♦ 4♦ 5♦ 9♣ 9♦ 9♥ J♠',        # K-A-2 is no run
 'A♠ 2♠ 3♠ 4♠ 5♠ 7♣ 7♦ 7♥ 7♠ K♦',        # four of a kind beside a run
 '7♥ 7♣ 7♦ 5♥ 6♥ 8♥ 9♥ 2♠ 3♠ K♣',        # 7♥ wanted by the set and the run
 '4♣ 5♣ 6♣ 7♣ 4♦ 4♥ 6♦ 6♥ Q♠ K♠',        # a run or two sets
 '2♣ 2♦ 2♥ 2♠ 3♣ 3♦ 3♥ 4♣ 4♦ 4♥',        # sets, or runs of three suits
 'A♣ 2♣ 3♣ Q♦ K♦ A♦ 5♥ 6♥ 8♥ 9♥',        # ace low and ace high in different suits
 'J♠ Q♠ K♠ 10♦ J♦ Q♦ 10♥ J♥ Q♥ 10♣',     # rows or columns
 '10♠ J♠ Q♠ K♠ A♠ 9♠ 8♠ 7♠ 6♠ 5♠ 4♠',    # eleven in a suit
 'K♣ Q♦ J♥ 10♠ 9♣ 8♦ 7♥ 6♠ 5♣ 4♦',       # nothing melds
 '7♣ 7♦ 7♥ 7♠ 8♠ 9♠ 2♦ 3♣ 4♥ K♦',        # four of a kind split: three and a run
]
cases=[{'hand':H(h)} for h in curated]
rng=random.Random(1610)
for n in [10]*18+[11]*12:
    cases.append({'hand':rng.sample(DECK,n)})
for c in cases: c['deadwood']=best(c['hand'])
lay_curated=[
 ('4♥ 3♥ 8♥ K♣ K♦ 2♣', ['5♥ 6♥ 7♥']),               # both ends of a run
 ('9♣ 9♦ 9♥ 2♠ 3♠ Q♦', ['9♠ 10♠ J♠']),              # 9♠ is theirs; nothing lays off, 9s stay a set
 ('5♥ 5♣ 5♦ 4♥ K♠ 2♦', ['6♥ 7♥ 8♥']),               # 5♥ in a set, or laid off with 4♥
 ('A♠ 2♠ K♥ Q♣', ['3♠ 4♠ 5♠']),                     # A-2 below a run
 ('J♦ Q♦ K♦ A♦ 3♣', ['8♦ 9♦ 10♦']),                  # a whole run lays off, ace high
 ('7♣ 3♠ 4♠ Q♥', ['7♥ 7♠ 7♦']),                     # the fourth of a set
]
lay=[{'hand':H(h),'onto':[H(m) for m in o]} for h,o in lay_curated]
for _ in range(14):
    d=rng.sample(DECK,52)
    # a knocker's melds drawn from a real arrangement: a run and a set
    s=rng.choice(SUITS); start=rng.randint(0,9)
    run=[RANKS[(start+i)] + s for i in range(3)]
    r=rng.choice([x for x in RANKS if x+s not in run])
    sset=[r+t for t in SUITS if r+t not in run][:3]
    used=set(run+sset)
    hand=[c for c in d if c not in used][:rng.choice([7,9,10])]
    lay.append({'hand':hand,'onto':[run,sset]})
for c in lay: c['deadwood']=best_layoff(c['hand'],c['onto'])
print(json.dumps({'arrange':cases,'layOff':lay},ensure_ascii=False,indent=1))
