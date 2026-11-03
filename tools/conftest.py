import os
import sys

HERE = os.path.dirname(__file__)
for sub in ('fitcheck', 'gen_ride', 'reference'):
    sys.path.insert(0, os.path.join(HERE, sub))
