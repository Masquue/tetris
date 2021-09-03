#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <cstring>
#include <ncurses.h>
#include <unistd.h>

struct gameover : std::exception { };

class tetris {
public:
    tetris(int height = 20, int width = 10) 
        : height_(height),
          width_(width),
          board_(height_, std::vector<int>(width_, 0))
    {
        initscr();
        noecho();
        curs_set(0);
        timeout(0);

        start_color();
        for (int i = 1; i <= 7; ++i)
            init_pair(i, 0, i);

        // 2*width: two characters for a block
        // +2 for the border
        resizeterm(height + 2, 2 * width + 2);

        box(stdscr, 0, 0);
        new_piece();
        frame();
    }

    ~tetris() {
        endwin();
    }

    // void set_colors();

    void start() {
        loop();
    }

private:
    struct block {
        int y, x;

        block operator+(const block& b) const {
            return { y + b.y, x + b.x };
        }

        void rotate(int cnt) {
            // different from normal coordinates
            // y axis point to below
            // also using (y, x) coordinates
            // so reverse revsese = same
            static const int cw[2][2] = { { 0, 1}, {-1, 0} },
                            ccw[2][2] = { { 0,-1}, { 1, 0} };
            // default clockwise
            static const int (&rot)[2][2] = cw;
            cnt %= 4;
            while (cnt--) {
                int new_y = y * rot[0][0] + x * rot[0][1];
                int new_x = y * rot[1][0] + x * rot[1][1];
                y = new_y;
                x = new_x;
            }
        }
    };

    struct extent {
        int y_min, y_max,
            x_min, x_max;
    };

    using shape_t = std::vector<block>;
    using block_it = std::vector<block>::const_iterator;

    struct piece {
        std::size_t shape_index;  // index into shapes_
        std::vector<block> shape;
        block location;
        int color;          // [1, 7)
        const tetris* t;

        block_it begin() const { return shape.cbegin(); }
        block_it   end() const { return shape.cend(); }

        void rotate(int cnt) {
            for (auto& block : shape)
                block.rotate(cnt);
        }

        extent get_extent() const {
            // assuming at least one block in a shape
            extent e{ shape[0].y, shape[0].y, shape[0].x, shape[0].x };
            for (std::size_t i = 1; i < shape.size(); ++i) {
                e.y_min = std::min(e.y_min, shape[i].y);
                e.y_max = std::max(e.y_max, shape[i].y);
                e.x_min = std::min(e.x_min, shape[i].x);
                e.x_max = std::max(e.x_max, shape[i].x);
            }
            return e;
        }
    };

    // false: cannot place new piece, i.e. gameover
    void new_piece() {
        static std::default_random_engine e(std::random_device{}());
        std::uniform_int_distribution<> shape_d(0, n_shapes_ - 1),
                                        rot_d(0, 3),
                                        color_d(1, 7);
        curr_piece_.shape_index = shape_d(e);
        curr_piece_.shape = shapes_[curr_piece_.shape_index];
        curr_piece_.color = color_d(e);
        curr_piece_.t = this;

        int rot = rot_d(e);
        curr_piece_.rotate(rot);

        auto ext = curr_piece_.get_extent();
        // we want the extent of rand_x in [0, width), rand_y+ext.y_min at 0 line
        // 0 <= rand_x + ext.x_min, rand_x + ext.x_max < width
        int x_min = -ext.x_min, x_max = width_ - ext.x_max - 1,
            y = -ext.y_min;
        if (x_min > x_max || y + ext.y_max >= height_)
            throw std::runtime_error("space to small");
        std::uniform_int_distribution<> x_d(x_min, x_max);
        curr_piece_.location = { -ext.y_min, x_d(e) };

        for (const auto& block : curr_piece_.shape) {
            if (board_cell(curr_piece_.location + block) != 0)
                throw gameover();
        }
        update_piece(curr_piece_.color);
    }

    bool can_move(int y, int x) {
        update_piece(0);
        // clear current piece before doing check
        bool movable = true;
        const auto& loc = curr_piece_.location;
        for (const auto& block : curr_piece_) {
            auto block_loc = loc + block;
            if (!check_boundary(block_loc, y, x) ||
                    board_cell(block_loc, y, x) != 0){
                movable = false;
                break;
            }
        }
        update_piece(curr_piece_.color);
        return movable;
    }

    // flase: can not move (hit ground or boundary)
    // true: move success
    bool move_piece(int y = 1, int x = 0) {
        if (!can_move(y, x))
            return false;
        update_piece(0);
        curr_piece_.location.y += y;
        curr_piece_.location.x += x;
        update_piece(curr_piece_.color);
        return true;
    }

    bool can_rotate(int cnt) {
        update_piece(0);
        // clear current piece before doing check
        bool rotatable = true;
        const auto& loc = curr_piece_.location;
        for (const auto& block : curr_piece_) {
            auto rotated = block;
            rotated.rotate(cnt);
            auto block_loc = loc + rotated;
            if (!check_boundary(block_loc) ||
                    board_cell(block_loc) != 0){
                rotatable = false;
                break;
            }
        }
        update_piece(curr_piece_.color);
        return rotatable;
    }

    // todo: some rotation should be fixed
    bool rotate_piece(int cnt = 1) {
        if (!can_rotate(cnt))
            return false;
        update_piece(0);
        curr_piece_.rotate(cnt);
        update_piece(curr_piece_.color);
        return true;
    }

    // update curr piece, do not check boundaries
    void update_piece(int color, int y = 0, int x = 0) {
        const auto& loc = curr_piece_.location;
        for (const auto& block : curr_piece_) {
            board_cell(loc + block, y, x) = color;
        }
    }

    void clear_line(int y) {
        for (int x = 0; x < width_; ++x)
            board_[y][x] = 0;
    }

    void try_remove_line() {
        auto ext = curr_piece_.get_extent();
        std::set<int> lines_to_remove;
        for (int i = curr_piece_.location.y + ext.y_max; 
                i >= curr_piece_.location.y + ext.y_min; --i) {
            if (std::all_of(board_[i].cbegin(), board_[i].cend(), [](int c) { return c != 0; }))
                lines_to_remove.insert(i);
        }
        if (lines_to_remove.empty())
            return;
        
        score_ += lines_to_remove.size();

        std::vector<bool> moved(height_, false);
        for (int y = *lines_to_remove.crbegin(); y >= 1; --y) {
            int from = y - 1;
            while (from >= 0 &&
                   (lines_to_remove.count(from) == 1 || moved[from]))
                --from;
            if (from >= 0) {
                for (int x = 0; x < width_; ++x)
                    board_[y][x] = board_[from][x];
                moved[from] = true;
            } else
                clear_line(y);
        }
        clear_line(0);
    }

    bool check_boundary(const block& b, int y = 0, int x = 0) const {
        int yy = b.y + y, xx = b.x + x;
        return 0 <= yy && yy < height_ &&
               0 <= xx && xx < width_;
    }

    int& board_cell(const block& b, int y = 0, int x = 0) {
        return board_[b.y + y][b.x + x];
    }

    const int& board_cell(const block& b, int y = 0, int x = 0) const {
        auto& ret = const_cast<tetris*>(this)->board_cell(b, y, x);
        return const_cast<const int&>(ret);
    }

    void tick() {
        if (++tick_cnt_ >= move_ticks_) {
            tick_cnt_ = 0;
            if (!move_piece()) {
                try_remove_line();
                new_piece();
            }
            frame();
        }
    }

    void frame() {
        for (int y = 0; y < height_; ++y) {
            move(y + 1, 1);
            for (int x = 0; x < width_; ++x) {
                attron(COLOR_PAIR(board_[y][x]));
                printw("  ");
                attroff(COLOR_PAIR(board_[y][x]));
            }
        }
        move(height_ + 1, 1);
        printw("score: %i", score_);
        refresh();
    }

    // n is the length of text to be put at center
    int move_start_of_center(int y, int n) {
        return move(y, (COLS - n) / 2);
    }

    int mvaddstr_center(int y, const char* str) {
        move_start_of_center(y, std::strlen(str));
        return printw(str);
    }
    
    void loop() {
        try {
            while (true) {
                usleep(1000000 / tick_times_);
                tick();
                char c;
                if ((c = getch()) != ERR) {
                    if (c == 'w')
                        rotate_piece();
                    else if (c == 'a')
                        move_piece(0, -1);
                    else if (c == 'd')
                        move_piece(0, 1);
                    else if (c == 's') {
                        while (move_piece())
                            ;
                        try_remove_line();
                        tick_cnt_ = 0;  // reset tick
                        new_piece();    // pop up next piece immediately
                    }
                    if (c == 'q')
                        break;
                    frame();
                }
            }
        } catch(const gameover& gg) {
        }
        mvaddstr_center(height_ / 2 + 1, "GAME OVER");
        timeout(-1);
        getch();
    }

private:
    const int tick_times_ = 100;    // ticks per second
    const double move_intervals_ = 0.5;  // interval between moves (unit second) (second per move)
    const int move_ticks_ = std::floor(tick_times_ * move_intervals_); // ticks per move

    const std::vector<shape_t> shapes_ = {
        {{ 0, -1}, { 0,  0}, { 0,  1}, { 0,  2}},
        {{ 0,  0}, { 0,  1}, { 1,  0}, { 1,  1}},
        {{-1,  0}, { 0,  0}, { 0,  1}, { 1,  1}},
        {{-1,  0}, { 0,  0}, { 0, -1}, { 1, -1}},
        {{ 1, -1}, { 0, -1}, { 0,  0}, { 0,  1}},
        {{ 1,  1}, { 0,  1}, { 0,  0}, { 0, -1}},
        {{ 0,  0}, {-1,  0}, { 0, -1}, { 0,  1}},
    };
    const std::size_t n_shapes_ = shapes_.size();

    int tick_cnt_ = 0;
    std::size_t score_ = 0;
    const int height_, width_;
    std::vector<std::vector<int>> board_;
    piece curr_piece_;
};

int main()
{
    tetris t;
    t.start();
}
