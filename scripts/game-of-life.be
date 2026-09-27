# name: Game of Life
# summary: Conway's cells breed and die across the panel, reseeding when they stall.
# author: Galadril
# tags: animation, generative, classic
# panel: 52x16

import math

# Conway's Game of Life on the full 52x16 grid.
#
# The interesting thing about Life on a panel this small is that most seeds
# settle into a still life or a short blinker within a few hundred
# generations - so this watches for a stall and reseeds, which is what makes
# it something to leave running rather than a thing that stops.
#
# Written against the instruction budget from the start. The obvious shape -
# a neighbour() method called eight times per cell - is 6,656 calls per
# generation on this grid and does not fit. Everything below is written the
# awkward way on purpose; see the comments in step().

class App
  var w, h
  var cells, next    # flat width*height boolean arrays, current and scratch
  var stale, seen    # stall detection: population, and how long it held
  var last           # when the last generation ran, on the device clock

  def init()
    self.w = width()
    self.h = height()
    self.last = 0
    self.seed()
  end

  def seed()
    self.cells = []
    self.next = []
    var i = 0
    var total = self.w * self.h
    while i < total
      # About 30% filled gives lively first generations without saturating
      # into large blocks that immediately die of overcrowding.
      self.cells.push((math.rand() % 10) < 3)
      self.next.push(false)
      i += 1
    end
    self.stale = -1
    self.seen = 0
  end

  def step()
    # Locals, and the neighbours written out rather than fetched through a
    # method.
    #
    # Life reads eight neighbours per cell, so on 832 cells anything done
    # per-neighbour happens 6,656 times a generation. A method call there is
    # what put the first version of this over the budget, where it lost every
    # frame it tried to step on.
    #
    # The edge wrapping - so gliders leave one side and return on the other
    # rather than piling against a wall - is hoisted: rows above and below
    # once per row, columns once per cell.
    var c = self.cells
    var nx = self.next
    var w = self.w
    var h = self.h
    var pop = 0

    var y = 0
    while y < h
      var up = y - 1
      if up < 0 up = h - 1 end
      var dn = y + 1
      if dn >= h dn = 0 end

      var rowUp = up * w
      var row = y * w
      var rowDn = dn * w

      var x = 0
      while x < w
        var lf = x - 1
        if lf < 0 lf = w - 1 end
        var rt = x + 1
        if rt >= w rt = 0 end

        var n = 0
        if c[rowUp + lf] n += 1 end
        if c[rowUp + x]  n += 1 end
        if c[rowUp + rt] n += 1 end
        if c[row + lf]   n += 1 end
        if c[row + rt]   n += 1 end
        if c[rowDn + lf] n += 1 end
        if c[rowDn + x]  n += 1 end
        if c[rowDn + rt] n += 1 end

        var alive = c[row + x]
        var live = (alive && (n == 2 || n == 3)) || (!alive && n == 3)
        nx[row + x] = live
        if live pop += 1 end
        x += 1
      end
      y += 1
    end

    var tmp = self.cells
    self.cells = self.next
    self.next = tmp

    # A steady population means the board has settled into still lifes and
    # blinkers. Reseed before it becomes wallpaper.
    if pop == self.stale
      self.seen += 1
      if self.seen > 40 || pop == 0
        self.seed()
      end
    else
      self.stale = pop
      self.seen = 0
    end
  end

  def draw()
    # Eight generations a second, not thirty.
    #
    # On its own clock rather than the frame rate, so it runs at the same
    # speed whatever else the panel is doing - and because stepping on every
    # frame is both eight times the work and far faster than Life is worth
    # watching at.
    #
    # now_ms() is the device clock, so this keeps its cadence across the
    # carousel taking the app away and bringing it back.
    var now = now_ms()
    if now - self.last >= 125
      self.last = now
      self.step()
    end

    clear(rgb(0, 0, 0))

    var c = self.cells
    var w = self.w
    var i = 0
    var y = 0
    while y < self.h
      # The gradient is computed per row, so rgb() is called sixteen times a
      # frame rather than once per living cell.
      var colour = rgb(20, 150 + (y * 80) / self.h, 60)
      var x = 0
      while x < w
        if c[i] pixel(x, y, colour) end
        i += 1
        x += 1
      end
      y += 1
    end
  end
end

return App()
