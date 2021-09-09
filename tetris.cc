#include <algorithm>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <cstring>
#include <ncurses.h>
#include <unistd.h>

namespace {
    // n is the length of text to be put at center
    int move_start_of_center(int y, int n) {
        return move(y, (COLS - n) / 2);
    }

    int mvaddstr_center(int y, const char* str) {
        move_start_of_center(y, std::strlen(str));
        return printw(str);
    }
}

struct gameover : std::exception { };

class tetris {
public:
    tetris(int height = 20, int width = 10) 
        : height_(height),
          width_(width),
          board_height_(height_ + invisible_lines_),
          board_(board_height_, std::vector<int>(width_, 0))
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
    };

    struct extent {
        int y_min, y_max,
            x_min, x_max;
    };

    using shape_t = std::vector<block>;
    using block_it = std::vector<block>::const_iterator;

    struct piece {
        std::size_t shape;  // index into shapes_
        block location;
        int rotation;
        int color;          // [1, 7]
        const tetris* t;

        block_it begin() const { return get_shape().cbegin(); }
        block_it   end() const { return get_shape().cend(); }

        void rotate(bool ccw) {
            rotation = get_rotated(ccw);
        }

        int get_rotated(bool ccw) const {
            int rot = rotation;
            ccw ? --rot : ++rot;
            int n_rot = t->shapes_[shape].size();   // avoid mixing signed and unsigned
            rot = (rot + n_rot) % n_rot;
            return rot;
        }

        const shape_t& get_shape() const {
            return t->shapes_[shape][rotation];
        }

        const shape_t& get_rotated_shape(bool ccw) const {
            return t->shapes_[shape][get_rotated(ccw)];
        }

        extent get_extent() const {
            // assuming at least one block in a shape
            auto& shape = get_shape();
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

    // see https://tetris.wiki/Tetris_(NES,_Nintendo)
    int random_shape() {
        static std::default_random_engine e(std::random_device{}());

        std::uniform_int_distribution<> shape_d1(0, n_shapes_);
        int ret = shape_d1(e);
        if (ret == static_cast<int>(n_shapes_) || ret == prev_shape_) {
            std::uniform_int_distribution<> shape_d2(0, n_shapes_ - 1);
            ret = shape_d2(e);
        }
        prev_shape_ = ret;
        return ret;
    }

    // false: cannot place new piece, i.e. gameover
    void new_piece() {
        static std::default_random_engine e(std::random_device{}());

        std::uniform_int_distribution<> shape_d(0, n_shapes_ - 1),
                                        color_d(1, 7);
        curr_piece_.shape = random_shape();
        curr_piece_.color = color_d(e);
        auto n_rot = shapes_[curr_piece_.shape].size();
        std::uniform_int_distribution<> rot_d(0, n_rot - 1);
        curr_piece_.rotation = rot_d(e);
        curr_piece_.t = this;

        auto ext = curr_piece_.get_extent();
        // we want the extent of rand_x in [0, width), y+ext.y_min at first visible line
        // 0 <= rand_x + ext.x_min, rand_x + ext.x_max < width
        int x_min = -ext.x_min, x_max = width_ - ext.x_max - 1,
            y = invisible_lines_ - ext.y_min;
        if (x_min > x_max || y + ext.y_max >= board_height_)
            throw std::runtime_error("space too small");
        std::uniform_int_distribution<> x_d(x_min, x_max);
        curr_piece_.location = { y, x_d(e) };

        for (const auto& block : curr_piece_) {
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

    bool can_rotate(bool ccw) {
        update_piece(0);
        // clear current piece before doing check
        bool rotatable = true;
        const auto& loc = curr_piece_.location;
        for (const auto& block : curr_piece_.get_rotated_shape(ccw)) {
            auto block_loc = loc + block;
            if (!check_boundary(block_loc) ||
                    board_cell(block_loc) != 0){
                rotatable = false;
                break;
            }
        }
        update_piece(curr_piece_.color);
        return rotatable;
    }

    bool rotate_piece(bool ccw = false) {
        if (!can_rotate(ccw))
            return false;
        update_piece(0);
        curr_piece_.rotate(ccw);
        update_piece(curr_piece_.color);
        return true;
    }

    // update curr piece, do not check boundaries
    void update_piece(int color) {
        const auto& loc = curr_piece_.location;
        for (const auto& block : curr_piece_) {
            board_cell(loc + block) = color;
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

        std::vector<bool> moved(board_height_, false);
        for (int y = *lines_to_remove.crbegin(); y >= invisible_lines_ + 1; --y) {
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
        clear_line(invisible_lines_);
    }

    bool check_boundary(const block& b, int y = 0, int x = 0) const {
        int yy = b.y + y, xx = b.x + x;
        return 0 <= yy && yy < board_height_ &&
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
        for (int y = invisible_lines_; y < board_height_; ++y) {
            move(y - invisible_lines_ + 1, 1);
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
                        tick_cnt_ = 0;
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

    // use right-handed Nintendo Rotation System
    // see https://tetris.wiki/Nintendo_Rotation_System
    const std::vector<std::vector<shape_t>> shapes_ = {
        {   // I piece
            {{ 0, -2}, { 0, -1}, { 0,  0}, { 0,  1}},
            {{-2,  0}, {-1,  0}, { 0,  0}, { 1,  0}},
        },
        {   // O piece
            {{ 0,  0}, { 0,  1}, { 1,  0}, { 1,  1}},
        },
        {   // J piece
            {{ 1,  1}, { 0,  1}, { 0,  0}, { 0, -1}},
            {{ 1, -1}, { 1,  0}, { 0,  0}, {-1,  0}},
            {{-1, -1}, { 0, -1}, { 0,  0}, { 0,  1}},
            {{-1,  1}, {-1,  0}, { 0,  0}, { 1,  0}},
        },
        {   // L piece
            {{ 1, -1}, { 0, -1}, { 0,  0}, { 0,  1}},
            {{-1, -1}, {-1,  0}, { 0,  0}, { 1,  0}},
            {{-1,  1}, { 0,  1}, { 0,  0}, { 0, -1}},
            {{ 1,  1}, { 1,  0}, { 0,  0}, {-1,  0}},
        },
        {   // S piece
            {{-1,  0}, { 0,  0}, { 0,  1}, { 1,  1}},
            {{ 0,  1}, { 0,  0}, { 1,  0}, { 1, -1}},
        },
        {   // Z piece
            {{-1,  1}, { 0,  1}, { 0,  0}, { 1,  0}},
            {{ 1,  1}, { 1,  0}, { 0,  0}, { 0, -1}},
        },
        {   // T piece
            {{ 0,  0}, {-1,  0}, { 0, -1}, { 0,  1}},
            {{ 0,  0}, {-1,  0}, { 1,  0}, { 0,  1}},
            {{ 0,  0}, { 0, -1}, { 1,  0}, { 0,  1}},
            {{ 0,  0}, { 0, -1}, { 1,  0}, {-1,  0}},
        },
    };
    const std::size_t n_shapes_ = shapes_.size();

    int tick_cnt_ = 0;
    int prev_shape_ = -1;
    std::size_t score_ = 0;

    const int invisible_lines_ = 2;
    const int height_, width_,
              board_height_;
    std::vector<std::vector<int>> board_;
    piece curr_piece_;
};

int main()
{
    tetris t;
    t.start();
}
