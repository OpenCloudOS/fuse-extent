import os
import sys
import re

def execCmd(cmd):  
    r = os.popen(cmd)  
    text = r.read()  
    r.close()  
    return text 

def test_cmd_opts(program):
    print("test_cmd_opts\n")
    cmd=program+" test_cmd_opts"
    reslutList=["0","0","0","0","0","0","0","(null)"]
    validoptions=["-h","--help","-v","--version","-d","--debug","-f","--foreground","-m","--multithread","-c","--clonefd","-t=1","--threads=1"]
    invalidoptions=["-n","-none","--n","--none"]
    erroptions=["-t=a","--threads=aa","-t==a","--threads==aa","-t=-10","--threads=-10"]
    validmountpoint="/root/fuse-extent/fusedir/testdir"

    # 有效命令
    for i in range(len(validoptions)):
        newcmd=cmd+" "+validoptions[i]+" "+validmountpoint
        expected=reslutList[:]
        expected[int(i/2)]="1"
        if validoptions[i]== "-d" or validoptions[i]=="--debug":
            expected[3]="1"
        expected[-1]=validmountpoint
        expect=','.join(expected)
        expect="cmd: "+expect+"; unknown: 2\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)
    
    # 包含一个额外的无效命令
    for opt in invalidoptions:
        newcmd=cmd+" "+opt+" "+validmountpoint
        expected=reslutList[:]
        expected[-1]=validmountpoint
        expect=','.join(expected)
        expect="cmd: "+expect+"; unknown: 3\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)
    
    for opt in erroptions:
        newcmd=cmd+" "+opt+" "+validmountpoint
        expect="-1\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)

    # 无效挂载点
    invalidmountpoint="/root/fuse-extent/fusedir/none"
    newcmd=cmd+" "+invalidmountpoint
    expect="-1\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

def test_mnt_opts(program):
    print("test_mnt_opts\n")
    cmd=program+" test_mnt_opts"
    validoptions=["--allow_other","--auto_unmount","--fsname=name","--fstype=type","--flags=ro"]
    invalidoptions1=["-allow_other","--n"]
    invalidoptions2=["--flags=123","--flags=ab,cd"]

    newcmd=cmd+" --allow_other"
    expect="mnt: 1,0,6,rw,nosuid,nodev,exec,async,atime,(null),(null); unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" --auto_unmount"
    expect="mnt: 0,1,6,rw,nosuid,nodev,exec,async,atime,(null),(null); unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" --fsname=name"
    expect="mnt: 0,0,6,rw,nosuid,nodev,exec,async,atime,name,(null); unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" --subtype=type"
    expect="mnt: 0,0,6,rw,nosuid,nodev,exec,async,atime,(null),type; unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" --flags=ro"
    expect="mnt: 0,0,7,ro,nosuid,nodev,exec,async,atime,(null),(null); unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)
    
    # 包含一个额外的无效命令
    for opt in invalidoptions1:
        newcmd=cmd+" "+opt
        expect="mnt: 0,0,6,rw,nosuid,nodev,exec,async,atime,(null),(null); unknown: 3\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)
    
    newcmd=cmd+" --flags=123"
    result=execCmd(newcmd)
    expect="mnt: 0,0,6,rw,nosuid,nodev,exec,async,atime,(null),(null); unknown: 2\n"
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" --flags=ab,cd"
    result=execCmd(newcmd)
    expect="mnt: 0,0,6,rw,nosuid,nodev,exec,async,atime,(null),(null); unknown: 2\n"
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)



def test_conn_info(program):
    print("test_conn_info\n")
    cmd=program+" test_conn_info"
    reslutList=["0","0","0","0","0","1"]
    validoptions=["--max_write=1","--max_read=1","--max_readahead=1","--max_background=1","--congestion_threshold=1","--time_gran=1"]
    invalidoptions=["--max_write","-max_write","--haha","--haha=1"]
    erroptions=["--max_write=-1","--max_write==1","--max_write=x"]

    # 有效命令
    for i in range(len(validoptions)):
        newcmd=cmd+" "+validoptions[i]
        expected=reslutList[:]
        expected[i]="1"
        expect=','.join(expected)
        expect="conn: "+expect+"; unknown: 2\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)

    # 包含一个额外的无效命令
    for opt in invalidoptions:
        newcmd=cmd+" "+opt
        expected=reslutList[:]
        expect=','.join(expected)
        expect="conn: "+expect+"; unknown: 3\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)
    
    # 会出错的命令
    for opt in erroptions:
        newcmd=cmd+" "+opt
        expect="-1\n"
        result=execCmd(newcmd)
        print("expect: [",expect,"]","result: [",result,"]")
        res=result.find(expect)
        assert(res!=-1)

    newcmd=cmd+" "+"--time_gran=9"
    expected=reslutList[:]
    expected[-1]="10"
    expect=','.join(expected)
    expect="conn: "+expect+"; unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" "+"--time_gran=10"
    expected=reslutList[:]
    expected[-1]="10"
    expect=','.join(expected)
    expect="conn: "+expect+"; unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

    newcmd=cmd+" "+"--time_gran=11"
    expected=reslutList[:]
    expected[-1]="100"
    expect=','.join(expected)
    expect="conn: "+expect+"; unknown: 2\n"
    result=execCmd(newcmd)
    print("expect: [",expect,"]","result: [",result,"]")
    res=result.find(expect)
    assert(res!=-1)

if __name__ == '__main__':
    program=sys.argv[1]
    assert(program!=None)
    test_cmd_opts(program)
    test_mnt_opts(program)
    test_conn_info(program)