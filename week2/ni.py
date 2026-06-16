import copy  # use it for deepcopy if needed
import math
import logging
import time  # Importing time to measure performance

logging.basicConfig(format='%(levelname)s - %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S',
                    level=logging.INFO)

# # Global variable to keep track of visited board positions.
# board_positions_val_dict = {}
# # Global variable to store the visited histories in the process of alpha beta pruning.
# visited_histories_list = []

DEBUG_DEPTH = 3

board_positions_val_dict = {}
visited_histories_list = []

cache_hits = 0

class History:
    def __init__(self, num_boards=2, history=None):
        self.num_boards = num_boards
        if history is not None:
            self.history = history
            self.boards = self.get_boards()
        else:
            self.history = []
            self.boards = []
            for i in range(self.num_boards):
                self.boards.append(['0', '0', '0', '0', '0', '0', '0', '0', '0'])
        # Maintain a list to keep track of active boards
        self.active_board_stats = self.check_active_boards()
        self.current_player = self.get_current_player()

    def get_boards(self):
        boards = []
        for i in range(self.num_boards):
            boards.append(['0', '0', '0', '0', '0', '0', '0', '0', '0'])
        for i in range(len(self.history)):
            board_num = math.floor(self.history[i] / 9)
            play_position = self.history[i] % 9
            boards[board_num][play_position] = 'x'
        return boards

    def check_active_boards(self):
        active_board_stat = []
        for i in range(self.num_boards):
            if self.is_board_win(self.boards[i]):
                active_board_stat.append(0)
            else:
                active_board_stat.append(1)
        return active_board_stat

    @staticmethod
    def is_board_win(board):
        for i in range(3):
            if board[3 * i] == board[3 * i + 1] == board[3 * i + 2] != '0':
                return True
            if board[i] == board[i + 3] == board[i + 6] != '0':
                return True
        if board[0] == board[4] == board[8] != '0':
            return True
        if board[2] == board[4] == board[6] != '0':
            return True
        return False

    def get_current_player(self):
        total_num_moves = len(self.history)
        if total_num_moves % 2 == 0:
            return 1
        else:
            return 2

    def get_boards_str(self):
        boards_str = ""
        for i in range(self.num_boards):
            boards_str = boards_str + ''.join([str(j) for j in self.boards[i]])
        return boards_str

    def is_win(self):
        if 1 not in self.check_active_boards():
            return True
        else:
            return False

    def get_valid_actions(self):
        valid_actions = []
        active_boards = self.active_board_stats
        board_current = self.boards
        for i in range(self.num_boards):
            if active_boards[i] == 1:
                for j in range(9):
                    if board_current[i][j] == '0':
                        valid_actions.append(j+i*9)
        return valid_actions

    def is_terminal_history(self):
        if not self.get_valid_actions(): 
            return True
        else: 
            return False

    def get_value_given_terminal_history(self):
        return 1 if self.current_player == 1 else -1

    def ordered_valid_actions(self):
        o_valid_moves = []
        p_valid_actions = self.get_valid_actions()

        for p in p_valid_actions:
            if p%9 == 4:
                o_valid_moves.append(p)
        for p in p_valid_actions:
            if p%9 in [0, 2, 6, 8]:
                o_valid_moves.append(p)
        for p in p_valid_actions:
            if p not in o_valid_moves:
                o_valid_moves.append(p)
        
        return o_valid_moves


def alpha_beta_pruning(history_obj, alpha, beta, max_player_flag):
    global visited_histories_list
    global board_positions_val_dict
    global cache_hits

    state_key = (
        history_obj.get_boards_str(),
        tuple(history_obj.active_board_stats)
    )

    if state_key in board_positions_val_dict:
        cache_hits += 1
        return board_positions_val_dict[state_key]
    
    visited_histories_list.append(history_obj.history)

    # Print when arriving at depth 3
    if len(history_obj.history) == DEBUG_DEPTH:
        print(f"VISITING {history_obj.history}")

    valid_actions = history_obj.get_valid_actions()

    if not valid_actions:
        value = history_obj.get_value_given_terminal_history()

        board_positions_val_dict[state_key] = value

        if len(history_obj.history) == DEBUG_DEPTH:
            print(f"DONE     {history_obj.history}")

        return value

    if max_player_flag:
        best_value = -math.inf

        for p in valid_actions:
            child = History(
                num_boards=history_obj.num_boards,
                history=history_obj.history + [p]
            )

            value = alpha_beta_pruning(child, alpha, beta, False)

            best_value = max(best_value, value)
            alpha = max(alpha, best_value)

            if alpha >= beta:
                break

    else:
        best_value = math.inf

        for p in valid_actions:
            child = History(
                num_boards=history_obj.num_boards,
                history=history_obj.history + [p]
            )

            value = alpha_beta_pruning(child, alpha, beta, True)

            best_value = min(best_value, value)
            beta = min(beta, best_value)

            if alpha >= beta:
                break

    # Print when leaving depth 3
    if len(history_obj.history) == DEBUG_DEPTH:
        print(f"DONE     {history_obj.history}")

    board_positions_val_dict[state_key] = best_value
    return best_value


def solve_alpha_beta_pruning(history_obj, alpha, beta, max_player_flag):
    global visited_histories_list
    val = alpha_beta_pruning(history_obj, alpha, beta, max_player_flag)
    return val, visited_histories_list


if __name__ == "__main__":
    logging.info("Starting Benchmark Tests")
    
    # List of different board configurations to try
    # WARNING: 3 boards might take an extremely long time without memoization!
    board_test_sizes = [3] 

    for num_b in board_test_sizes:
        logging.info(f"--- Running Alpha-Beta for {num_b} Board(s) ---")
        
        # Clear global stats before each distinct run
        visited_histories_list.clear() 
        board_positions_val_dict.clear()
        cache_hits = 0 
        # Mark the high-precision start time
        start_time = time.perf_counter()
        
        # Initialize an empty board state
        initial_state = History(history=[], num_boards=num_b)
        value, visited = solve_alpha_beta_pruning(initial_state, -math.inf, math.inf, True)
        
        # Mark the end time and calculate the difference
        end_time = time.perf_counter()
        elapsed_time = end_time - start_time
        
        logging.info(f"Result for {num_b} Board(s): Maxmin Value = {value}")
        logging.info(f"Total Unique Paths Traversed: {len(visited)}")
        logging.info(f"Execution Time: {elapsed_time:.4f} seconds\n")
        logging.info(f"Cached States: {len(board_positions_val_dict)}")
        logging.info(f"Cache Hits: {cache_hits}")

    logging.info("Benchmark complete.")