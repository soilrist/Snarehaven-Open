# 사용: python3 ue_py.py "<파이썬 코드>" — 떠 있는 Snarehaven 에디터 안에서 실행하고 출력을 돌려받는다
import sys, time
sys.path.insert(0, "~/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python")
import remote_execution as re
r = re.RemoteExecution(); r.start(); t = time.time()
while not r.remote_nodes and time.time() - t < 10: time.sleep(0.3)
if not r.remote_nodes: print("노드 없음(에디터 원격 실행 꺼짐)"); r.stop(); sys.exit(1)
r.open_command_connection(r.remote_nodes[0]["node_id"])
res = r.run_command(sys.argv[1], unattended=True, exec_mode=re.MODE_EXEC_FILE)
print(res.get("result")); [print(o["output"], end="") for o in res.get("output", [])]
r.stop()
