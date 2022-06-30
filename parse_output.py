import numpy as np

with open("output.txt") as f:
    data = f.readlines()
    data = [int(i.split()[0]) if len(i.split()) > 0 else 100000 for i in data]

print "#samples: " + str(len(data))
print "p50: " + str(np.percentile(data, 50))
print "p99: " + str(np.percentile(data, 99))
