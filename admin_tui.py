import curses
import socket
import struct
import os
import sys

CPP_SERVER_IP = "127.0.0.1"
CPP_SERVER_PORT = 8888
CONFIG_FILE = "flags_config.txt"
ADMIN_TOKEN = "admin_secret_token_999"

def send_set_flag_request(name, enabled, rollout, group, val_type, val_str):
    # Prepare SetFlag payload
    payload = bytearray()
    payload.append(1 if enabled else 0)
    payload.append(rollout)

    group_bytes = group.encode('utf-8')
    payload.append(len(group_bytes))
    payload.extend(group_bytes)

    name_bytes = name.encode('utf-8')
    payload.extend(struct.pack('>H', len(name_bytes)))
    payload.extend(name_bytes)

    if val_type == 'bool':
        payload.append(0x01)
        payload.append(1 if val_str.lower() in ('true', '1', 'on', 'yes') else 0)
    elif val_type == 'double':
        payload.append(0x02)
        try:
            d_val = float(val_str)
        except ValueError:
            d_val = 0.0
        payload.extend(struct.pack('<d', d_val))
    elif val_type == 'string':
        payload.append(0x03)
        val_bytes = val_str.encode('utf-8')
        payload.extend(struct.pack('>H', len(val_bytes)))
        payload.extend(val_bytes)
    else:
        raise ValueError(f"Unknown type: {val_type}")

    set_flag_header = struct.pack('>BBH', 0x46, 0x04, len(payload))

    # Connect and perform stateful authorization + command transaction
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((CPP_SERVER_IP, CPP_SERVER_PORT))
            
            # 1. Send CMD_AUTH Handshake
            token_bytes = ADMIN_TOKEN.encode('utf-8')
            auth_header = struct.pack('>BBH', 0x46, 0x05, len(token_bytes)) # 0x05 = CMD_AUTH
            s.sendall(auth_header + token_bytes)
            
            # Read Auth Response (3 bytes)
            auth_response = s.recv(3)
            if len(auth_response) < 3:
                return False, "C++ Server disconnected during auth handshake."
            magic, status, _ = struct.unpack('>BBB', auth_response)
            if magic != 0x46 or status != 0:
                return False, f"Authentication failed. Status: {status}"

            # 2. Send CMD_SET_FLAG
            s.sendall(set_flag_header + payload)
            
            # Read SetFlag Response
            response = s.recv(3)
            if len(response) < 3:
                return False, "C++ Server disconnected during update command."
            magic, status, _ = struct.unpack('>BBB', response)
            if status != 0:
                return False, f"Server rejected update. Status: {status}"
            return True, "Update successfully pushed to C++ server!"
    except Exception as e:
        return False, f"Connection error: {str(e)}"

def read_current_flags():
    flags = []
    if not os.path.exists(CONFIG_FILE):
        return flags
    
    with open(CONFIG_FILE, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(',')
            if len(parts) < 6:
                continue
            key = parts[0]
            enabled = parts[1] == '1'
            group = parts[2]
            try:
                rollout = int(parts[3])
            except ValueError:
                rollout = 100
            val_type_char = parts[4]
            val_str = ",".join(parts[5:])
            
            val_type = "string"
            if val_type_char == 'b': val_type = "bool"
            elif val_type_char == 'n': val_type = "double"
            
            flags.append({
                'key': key,
                'enabled': enabled,
                'group': group,
                'rollout': rollout,
                'val_type': val_type,
                'value': val_str
            })
    return flags

def write_flags_to_file(flags):
    with open(CONFIG_FILE, 'w') as f:
        for flag in flags:
            type_char = 's'
            if flag['val_type'] == 'bool': type_char = 'b'
            elif flag['val_type'] == 'double': type_char = 'n'
            
            enabled_char = '1' if flag['enabled'] else '0'
            f.write(f"{flag['key']},{enabled_char},{flag['group']},{flag['rollout']},{type_char},{flag['value']}\n")

def get_input(stdscr, y, x, prompt_text):
    curses.noecho()
    stdscr.addstr(y, x, prompt_text + " ")
    stdscr.clrtoeol()
    stdscr.refresh()
    
    input_str = []
    curr_x = x + len(prompt_text) + 1
    
    # Enable cursor during prompt input
    curses.curs_set(1)
    
    while True:
        ch = stdscr.getch()
        
        if ch == 27: # ESC key
            curses.curs_set(0)
            return None # Cancelled
            
        elif ch in (10, 13): # Enter key
            break
            
        elif ch in (8, 127, curses.KEY_BACKSPACE): # Backspace
            if len(input_str) > 0:
                input_str.pop()
                curr_x -= 1
                stdscr.move(y, curr_x)
                stdscr.addch(' ')
                stdscr.move(y, curr_x)
                stdscr.refresh()
                
        elif 32 <= ch <= 126: # Printable ascii characters
            if len(input_str) < 40: # Prevent buffer overflows
                input_str.append(chr(ch))
                stdscr.addch(y, curr_x, ch)
                curr_x += 1
                stdscr.refresh()
                
    curses.curs_set(0) # Hide cursor again
    return "".join(input_str).strip()

def main_tui(stdscr):
    # Setup color pairs
    curses.start_color()
    curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN) # Highlighted item
    curses.init_pair(2, curses.COLOR_GREEN, curses.COLOR_BLACK) # Enabled status
    curses.init_pair(3, curses.COLOR_RED, curses.COLOR_BLACK) # Disabled status
    curses.init_pair(4, curses.COLOR_YELLOW, curses.COLOR_BLACK) # Alerts/Info
    
    curses.curs_set(0) # Hide cursor
    stdscr.keypad(True)
    
    flags = read_current_flags()
    current_idx = 0
    status_msg = "TUI Controller Initialized."
    
    while True:
        stdscr.clear()
        
        # Check window size first
        height, width = stdscr.getmaxyx()
        if height < 24 or width < 80:
            stdscr.addstr(1, 2, f"Terminal window too small ({width}x{height})!", curses.A_BOLD)
            stdscr.addstr(2, 2, "Please expand/resize your terminal window to at least 80x24.", curses.A_BOLD)
            stdscr.addstr(4, 2, "Press 'q' to quit.", curses.A_DIM)
            stdscr.refresh()
            key = stdscr.getch()
            if key == ord('q'):
                break
            continue
        
        # Header Box
        stdscr.addstr(1, 2, "┌──────────────────────────────────────────────────────────────┐", curses.A_BOLD)
        stdscr.addstr(2, 2, "│    💎  HIGH-PERFORMANCE FEATURE FLAG CONTROLLER (TUI)   │", curses.A_BOLD)
        stdscr.addstr(3, 2, "└──────────────────────────────────────────────────────────────┘", curses.A_BOLD)
        
        # Table Header
        stdscr.addstr(5, 2, f"{'Flag Key':<20} | {'Status':<6} | {'Target Group':<12} | {'Rollout':<7} | {'Type':<6} | {'Value':<12}")
        stdscr.addstr(6, 2, "-" * 75)
        
        # Draw Table Rows
        if not flags:
            stdscr.addstr(8, 4, "No flags loaded. Check config file path.", curses.color_pair(3))
        else:
            for idx, flag in enumerate(flags):
                y = 7 + idx
                style = curses.A_NORMAL
                if idx == current_idx:
                    style = curses.color_pair(1) | curses.A_BOLD
                    stdscr.attron(style)
                    stdscr.addstr(y, 2, " " * 75) # Clear line background with selection color
                
                status_str = "ON" if flag['enabled'] else "OFF"
                status_color = curses.color_pair(2) if flag['enabled'] else curses.color_pair(3)
                if idx == current_idx:
                    status_color = curses.A_BOLD # keep highlight selection color text readable
                
                stdscr.addstr(y, 2, f"{flag['key']:<20} | ")
                
                # Draw status with color conditional
                if idx == current_idx:
                    stdscr.addstr(status_str)
                else:
                    stdscr.addstr(status_str, status_color)
                
                group_display = flag['group'] if flag['group'] else "Everyone"
                stdscr.addstr(f" | {group_display:<12} | {flag['rollout']:>3}%    | {flag['val_type']:<6} | {flag['value']:<12}")
                
                if idx == current_idx:
                    stdscr.attroff(style)
        
        # Draw Separator & Status message bar
        stdscr.addstr(18, 2, "=" * 75)
        stdscr.addstr(19, 2, f"Status: {status_msg}", curses.color_pair(4) | curses.A_BOLD)
        
        # Draw Keys Guide at bottom
        stdscr.addstr(21, 2, "KEYS: [↑/↓] Navigate | [Space] Toggle | [Enter] Edit Value | [t] Type | [g] Group | [r] Rollout | [q] Quit", curses.A_DIM)
        stdscr.refresh()
        
        # Handle key input
        key = stdscr.getch()
        
        if key == ord('q'):
            break
            
        elif key == curses.KEY_UP:
            if current_idx > 0:
                current_idx -= 1
                
        elif key == curses.KEY_DOWN:
            if current_idx < len(flags) - 1:
                current_idx += 1
                
        elif key == ord(' '): # Toggle flag enabled
            if flags:
                flag = flags[current_idx]
                flag['enabled'] = not flag['enabled']
                write_flags_to_file(flags)
                success, msg = send_set_flag_request(flag['key'], flag['enabled'], flag['rollout'], flag['group'], flag['val_type'], flag['value'])
                status_msg = f"Toggled '{flag['key']}'. " + msg
                
        elif key == 10 or key == 13: # Enter key to edit value
            if flags:
                flag = flags[current_idx]
                prompt = f"Enter new {flag['val_type']} value for '{flag['key']}':"
                new_val = get_input(stdscr, 15, 2, prompt)
                
                if new_val is None:
                    status_msg = "Edit value cancelled."
                    continue
                
                # Simple validation
                valid = True
                if flag['val_type'] == 'bool' and new_val.lower() not in ('true', 'false', '1', '0', 'on', 'off'):
                    status_msg = "Error: Invalid boolean value! Use true/false/1/0."
                    valid = False
                elif flag['val_type'] == 'double':
                    try:
                        float(new_val)
                    except ValueError:
                        status_msg = "Error: Invalid double numerical value!"
                        valid = False
                
                if valid:
                    flag['value'] = new_val
                    write_flags_to_file(flags)
                    success, msg = send_set_flag_request(flag['key'], flag['enabled'], flag['rollout'], flag['group'], flag['val_type'], flag['value'])
                    status_msg = f"Updated '{flag['key']}' value to '{new_val}'. " + msg
                    
        elif key == ord('r'): # Edit Rollout %
            if flags:
                flag = flags[current_idx]
                prompt = f"Enter rollout % (0-100) for '{flag['key']}':"
                new_rollout_str = get_input(stdscr, 15, 2, prompt)
                if new_rollout_str is None:
                    status_msg = "Edit rollout cancelled."
                    continue
                try:
                    new_rollout = int(new_rollout_str)
                    if 0 <= new_rollout <= 100:
                        flag['rollout'] = new_rollout
                        write_flags_to_file(flags)
                        success, msg = send_set_flag_request(flag['key'], flag['enabled'], flag['rollout'], flag['group'], flag['val_type'], flag['value'])
                        status_msg = f"Updated '{flag['key']}' rollout to {new_rollout}%. " + msg
                    else:
                        status_msg = "Error: Rollout must be between 0 and 100."
                except ValueError:
                    status_msg = "Error: Rollout must be an integer."
                    
        elif key == ord('g'): # Edit Targeting Group
            if flags:
                flag = flags[current_idx]
                prompt = f"Enter targeting group for '{flag['key']}' (empty for everyone):"
                new_group = get_input(stdscr, 15, 2, prompt)
                if new_group is None:
                    status_msg = "Edit targeting group cancelled."
                    continue
                flag['group'] = new_group
                write_flags_to_file(flags)
                success, msg = send_set_flag_request(flag['key'], flag['enabled'], flag['rollout'], flag['group'], flag['val_type'], flag['value'])
                status_msg = f"Updated '{flag['key']}' group to '{new_group if new_group else 'Everyone'}'. " + msg
                
        elif key == ord('t'): # Cycle Value Type
            if flags:
                flag = flags[current_idx]
                old_type = flag['val_type']
                if old_type == 'bool':
                    new_type = 'double'
                    new_val = '0.0'
                elif old_type == 'double':
                    new_type = 'string'
                    new_val = 'default_str'
                else:
                    new_type = 'bool'
                    new_val = '0'
                
                flag['val_type'] = new_type
                flag['value'] = new_val
                write_flags_to_file(flags)
                success, msg = send_set_flag_request(flag['key'], flag['enabled'], flag['rollout'], flag['group'], flag['val_type'], flag['value'])
                status_msg = f"Cycled '{flag['key']}' type to '{new_type}' (value reset to '{new_val}'). " + msg

if __name__ == "__main__":
    curses.wrapper(main_tui)
