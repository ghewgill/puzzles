import urllib.request

page = urllib.request.urlopen("http://www.chiark.greenend.org.uk/~sgtatham/puzzles/").read().decode()
puzzles = page.split("""<span class="puzzle">""")
with open("descriptions.h", "w") as f:
    print("NSString *GameDescriptions[][2] = {", file=f)
    for p in puzzles[1:]:
        r = p.split("<tr>")
        name = r[1][r[1].index(">")+1:r[1].index("<", 1)]
        desc = r[4][r[4].index(">")+1:r[4].index("</td>", 2)].replace("\n", " ").rstrip()
        desc = desc.replace("<code>&gt;</code>", ">")
        assert "<" not in desc, desc
        assert '"' not in desc, desc
        print('    {{@"{}", @"{}"}},'.format(name, desc), file=f)
    print("};", file=f)
