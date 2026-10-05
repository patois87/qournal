-- Writes a test file with Xournal++ 1.3.8 itself: it fills the document it was started with (see make.sh) with
-- what its plugin interface can make, saves it and quits. The timer module calls the steps one after the other
-- from the main loop, after the document is open.

local after = package.loadlib(os.getenv("XOJ_TIMER"), "luaopen_timer")()
local image = os.getenv("XOJ_IMAGE")

local function wave(x0, y0, width, height, n)
  local xs, ys, ps = {}, {}, {}
  for i = 0, n do
    xs[#xs + 1] = x0 + width * i / n
    ys[#ys + 1] = y0 + height * math.sin(i / n * 2 * math.pi)
    ps[#ps + 1] = 0.3 + 0.7 * i / n
  end
  return xs, ys, ps
end

local function firstPage()
  app.setCurrentPage(1)
  app.changeCurrentPageBackground("lined")
  local xs, ys, ps = wave(60, 160, 300, 30, 40)
  app.addStrokes({
    strokes = {
      { x = xs, y = ys, pressure = ps, tool = "pen", width = 2.26, color = 0x3333cc },
      { x = { 60, 360 }, y = { 230, 230 }, tool = "highlighter", width = 12, color = 0xffff00 },
      { x = { 60, 160, 160, 60, 60 }, y = { 260, 260, 340, 340, 260 }, tool = "pen", width = 1.41,
        color = 0x008000, fill = 128 },
      { x = { 200, 400 }, y = { 270, 270 }, tool = "pen", width = 2, color = 0, lineStyle = "dash" },
      { x = { 200, 400 }, y = { 290, 290 }, tool = "pen", width = 2, color = 0xcc0000, lineStyle = "dot" },
      { x = { 200, 400 }, y = { 310, 310 }, tool = "pen", width = 2, color = 0x800080, lineStyle = "dashdot" },
    },
    allowUndoRedoAction = "grouped",
  })
  app.addSplines({
    splines = {
      { coordinates = { 60, 400, 120, 360, 180, 440, 240, 400, 240, 400, 300, 360, 360, 440, 420, 400 },
        tool = "pen", width = 1.41, color = 0xff6600, lineStyle = "plain" },
    },
    allowUndoRedoAction = "grouped",
  })
  app.addTexts({
    texts = {
      { text = "Written by Xournal++ 1.3.8", font = { name = "Sans", size = 14 }, color = 0x000000, x = 60, y = 60 },
      { text = "Zwei Zeilen: äöü ß\nund € & <tags>", font = { name = "Serif Bold", size = 12 }, color = 0x1259b9,
        x = 60, y = 90 },
    },
    allowUndoRedoAction = "grouped",
  })
  app.addImages({ images = { { path = image, x = 380, y = 480, maxWidth = 120 } }, allowUndoRedoAction = "grouped" })

  app.activateAction("layer-new-above-current")
  app.setCurrentLayerName("Second layer")
  app.addStrokes({
    strokes = { { x = { 60, 500 }, y = { 600, 650 }, tool = "pen", width = 4, color = 0x00aaaa } },
    allowUndoRedoAction = "grouped",
  })
end

local function pdfPage()
  app.setCurrentPage(2)
  app.addStrokes({
    strokes = { { x = { 50, 250, 250 }, y = { 300, 300, 400 }, tool = "pen", width = 1.41, color = 0xcc0000 } },
    allowUndoRedoAction = "grouped",
  })
  app.addTexts({
    texts = { { text = "On the PDF", font = { name = "Sans", size = 12 }, color = 0xcc0000, x = 60, y = 260 } },
    allowUndoRedoAction = "grouped",
  })
end

local function morePages()
  app.setCurrentPage(3)
  app.addStrokes({
    strokes = { { x = { 20, 70 }, y = { 20, 50 }, tool = "pen", width = 2, color = 0 } },
    allowUndoRedoAction = "grouped",
  })
  local backgrounds = { "graph", "dotted", "ruled", "staves", "isograph", "isodotted" }
  for i, background in ipairs(backgrounds) do
    app.activateAction("new-page-at-end")
    app.setCurrentPage(3 + i)
    app.changeCurrentPageBackground(background)
    app.setBackgroundName("Background " .. background)
    app.addStrokes({
      strokes = { { x = { 50, 300 }, y = { 100, 100 + 20 * i }, tool = "pen", width = 1.41, color = 0x404040 } },
      allowUndoRedoAction = "grouped",
    })
  end
  -- A page of its own size
  app.setPageSize(400, 300)
end

function initUi()
  local steps = { firstPage, pdfPage, morePages, function() app.activateAction("save") end,
                  function() app.activateAction("quit") end }
  local function run(i)
    if steps[i] then
      after(1000, function()
        steps[i]()
        run(i + 1)
      end)
    end
  end
  after(2000, function() run(1) end)
end
