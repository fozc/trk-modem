"""Opt-in bench NTP/reset check using the shared physical console."""
import argparse
import datetime
import json
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT / "tools/hwtest"))
from serial_io import ConsoleSession

RAW = r"#NTP: (\d{2})/(\d{2})/(\d{2}),(\d{2}):(\d{2}):(\d{2})"
ADJUSTED = r"NTP: (\d{2})/(\d{2})/(\d{2}) (\d{2}):(\d{2}):(\d{2}) \(tz=(-?\d+)\)"
STAMP = r"^(\d{2})/(\d{2})/(\d{2}) (\d{2}):(\d{2}):(\d{2}):\d{3}"


def verify_ntp(lines, timezone):
    text = "\n".join(lines)
    raw = re.search(RAW,text)
    adjusted = re.search(ADJUSTED,text)
    assert raw and adjusted, "fresh NTP source and adjusted calendar required"
    year,month,day,hour,minute,second = map(int,raw.groups())
    utc = datetime.datetime(2000+year,month,day,hour,minute,second)
    expected = utc + datetime.timedelta(hours=timezone)
    day,month,year,hour,minute,second,offset = map(int,adjusted.groups())
    actual = datetime.datetime(2000+year,month,day,hour,minute,second)
    assert offset == timezone, "unexpected configured UTC offset"
    assert actual == expected, "NTP offset calendar mismatch"
    assert "RTC rejected the received time" not in text, "RTC rejected NTP"
    # The adjusted calendar is logged BEFORE rtc_sync. Require a later
    # timestamped device line to prove the new software clock was applied.
    after = text[adjusted.end():].splitlines()
    clocks = []
    for line in after:
        match = re.search(STAMP,line)
        if match:
            year,month,day,hour,minute,second=map(int,match.groups())
            clocks.append(datetime.datetime(2000+year,month,day,hour,minute,second))
    assert any(0 <= (clock-expected).total_seconds() <= 30 for clock in clocks),         "no post-sync timestamp matching the adjusted NTP calendar"
    return {"utc":utc.isoformat(),"offset_hours":timezone,
            "expected":expected.isoformat(),"applied":actual.isoformat()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console",required=True)
    parser.add_argument("--timezone",required=True,type=int,choices=range(-12,15))
    parser.add_argument("--outdir",required=True,type=Path)
    args=parser.parse_args()
    args.outdir.mkdir(parents=True,exist_ok=True)
    console=ConsoleSession(args.console,logfile=str(args.outdir / "console.log"))
    try:
        assert console.send_and_wait("cslog on","Console logger started!",10)
        console.send("su admin");time.sleep(.4)
        console.send("admin");time.sleep(.4)
        console.mark();mark=console.cursor
        console.send("reset")
        assert console.wait_for(ADJUSTED,timeout_s=120,since=mark),             "no NTP sync after reset"
        console.drain(3)
        result=verify_ntp([line for _,line in console.new_lines(since=mark)],
                          args.timezone)
        (args.outdir / "result.json").write_text(json.dumps(result,indent=2))
        print("PASS: NTP UTC + configured offset applied after reset",result)
    finally:
        console.close()


if __name__ == "__main__":
    main()
