import json
import copy  # use it for deepcopy if needed
import math  # for math.inf
import logging

logging.basicConfig(format='%(levelname)s - %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S',
                    level=logging.INFO)

# Global variables in which you need to store player strategies (this is data structure that'll be used for evaluation)
# Mapping from histories (str) to probability distribution over actions
strategy_dict_x = {}
strategy_dict_o = {}


class History:
    def __init__(self, history=None):
        """
        # self.history : Eg: [0, 4, 2, 5]
            keeps track of sequence of actions played since the beginning of the game.
            Each action is an integer between 0-8 representing the square in which the move will be played as shown
            below.
              ___ ___ ____
             |_0_|_1_|_2_|
             |_3_|_4_|_5_|
             |_6_|_7_|_8_|

        # self.board
            empty squares are represented using '0' and occupied squares are either 'x' or 'o'.
            Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
            for board
              ___ ___ ____
             |_x_|___|_x_|
             |___|_o_|_o_|
             |___|___|___|

        # self.player: 'x' or 'o'
            Player whose turn it is at the current history/board

        :param history: list keeps track of sequence of actions played since the beginning of the game.
        """
        if history is not None:
            self.history = history
            self.board = self.get_board()
        else:
            self.history = []
            self.board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        self.player = self.current_player()

    def current_player(self):
        """ Player function
        Get player whose turn it is at the current history/board
        :return: 'x' or 'o' or None
        """
        total_num_moves = len(self.history)
        if total_num_moves < 9:
            if total_num_moves % 2 == 0:
                return 'x'
            else:
                return 'o'
        else:
            return None

    def get_board(self):
        """ Play out the current self.history and get the board corresponding to the history in self.board.

        :return: list Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
        """
        board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        for i in range(len(self.history)):
            if i % 2 == 0:
                board[self.history[i]] = 'x'
            else:
                board[self.history[i]] = 'o'
        return board

    def is_win(self):
        # check if the board position is a win for either players
        # Feel free to implement this in anyway if needed
        if self.board[0] == self.board[1] == self.board[2] and self.board[0] != '0':
            return self.board[0]
        elif self.board[3] == self.board[4] == self.board[5] and self.board[3] != '0':
            return self.board[3]
        elif self.board[6] == self.board[7] == self.board[8] and self.board[6] != '0':
            return self.board[6]
        elif self.board[0] == self.board[3] == self.board[6] and self.board[0] != '0':
            return self.board[0]
        elif self.board[1] == self.board[4] == self.board[7] and self.board[1] != '0':
            return self.board[1]
        elif self.board[2] == self.board[5] == self.board[8] and self.board[2] != '0':
            return self.board[2]
        elif self.board[0] == self.board[4] == self.board[8] and self.board[0] != '0':
            return self.board[0]
        elif self.board[2] == self.board[4] == self.board[6] and self.board[2] != '0':
            return self.board[2]
        else:
            return False
        pass

    def is_draw(self):
        # check if the board position is a draw
        # Feel free to implement this in anyway if needed
        for i in range(9):
            if self.board[i] == '0':
                return False
        return True
        pass

    def get_valid_actions(self):
        # get the empty squares from the board
        # Feel free to implement this in anyway if needed
        valid_actions = []
        for i in range(9):
            if self.board[i] == '0':
                valid_actions.append(i)
        return valid_actions
        pass

    def is_terminal_history(self):
        # check if the history is a terminal history
        # Feel free to implement this in anyway if needed
        if self.is_win():
            return True
        elif self.is_draw():
            return True
        else:
            return False
        pass

    def get_utility_given_terminal_history(self):
        # Feel free to implement this in anyway if needed
        winner = self.is_win()
        if winner == 'x':
            return 1
        elif winner == 'o':
            return -1
        else: return 0
        pass

    def update_history(self, action):
        # In case you need to create a deepcopy and update the history obj to get the next history object.
        # Feel free to implement this in anyway if needed
        new_history = copy.deepcopy(self.history)
        new_history.append(action)
        return History(new_history)
        pass


def backward_induction(history_obj):
    """
    :param history_obj: Histroy class object
    :return: best achievable utility (float) for th current history_obj
    """
    global strategy_dict_x, strategy_dict_o

    def alphabeta(position,alpha,beta):
        if position.is_terminal_history():
            return position.get_utility_given_terminal_history()
        
        mover = position.player

        if mover == 'x':
            best_value = -math.inf

            for p in position.get_valid_actions():
                child = position.update_history(p)
                value = alphabeta(child,alpha,beta)

                if value > best_value:
                    best_value = value

                alpha = max(best_value,alpha)

                if alpha >= beta:
                    break
                
        else:
            best_value = math.inf

            for p in position.get_valid_actions():
                child = position.update_history(p)
                value = alphabeta(child,alpha,beta)

                if value < best_value:
                    best_value = value

                beta = min(best_value,beta)

                if alpha >= beta:
                    break

        return best_value
    
    mover = history_obj.player
    best_action = None

    best_value = -math.inf

    if mover == 'x':
        for p in history_obj.get_valid_actions():
            child = history_obj.update_history(p)
            value = alphabeta(child, -math.inf, math.inf)

            if value > best_value:
                best_value = value
                best_action = p
    else:
        best_value = math.inf

        for p in history_obj.get_valid_actions():
            child = history_obj.update_history(p)
            value = alphabeta(child, -math.inf, math.inf)

            if value < best_value:
                best_value = value
                best_action = p

    return best_action

# def solve_tictactoe():
#     backward_induction(History())
#     with open('./policy_x.json', 'w') as f:
#         json.dump(strategy_dict_x, f)
#     with open('./policy_o.json', 'w') as f:
#         json.dump(strategy_dict_o, f)
#     return strategy_dict_x, strategy_dict_o


if __name__ == "__main__":
    logging.info("Start")
    # solve_tictactoe()
    logging.info("End")

