import sys
def flags(path):
    d=open(path,'rb').read()
    base=0x540
    def f(n):
        b=d[base+n//2]
        return (b>>4) if n%2==0 else (b&0x0f)
    return f
if __name__=='__main__':
    f=flags(sys.argv[1])
    want=[106,110,111,113,116,119,120,126,127,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,161,162,170,171,242,243,439,440,154,109,104,105,107,114,115]
    for n in want:
        print(f"{n}: {f(n)}")
