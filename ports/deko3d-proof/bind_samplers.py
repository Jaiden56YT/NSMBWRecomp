import re, sys
src = open(sys.argv[1]).read()
n = [0]
def sub(m):
    b = n[0]; n[0] += 1
    return f"layout(binding = {b}) uniform {m.group(1)}"
out = re.sub(r"(?m)^uniform (highp |mediump |lowp )?(sampler\w*|usampler\w*|isampler\w*)", lambda m: sub(type('M',(),{'group':lambda s,i: (m.group(1) or '') + m.group(2)})()), src)
open(sys.argv[2], 'w').write(out)
