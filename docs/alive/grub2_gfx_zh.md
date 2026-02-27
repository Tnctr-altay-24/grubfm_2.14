# 主题

注意：主题与文件读取进度功能冲突。如果出现图像显示方面的问题，请禁用进度显示。

`export enable_progress_indicator=0`

## 字体

GRUB 支持 PFF2 字体格式。可以用 `lsfonts` 命令列出可用字体，用 `loadfonts` 命令加载字体。

## 图片

GRUB 支持 bmp, jpg, jpeg, png, tga 格式的图片。

## 颜色

支持以下格式的颜色：

- \#RRGGBB (十六进制数字，例如 “#8899FF”)
- \#RGB (同上)
- R,G,B (十进制数字，例如 “128,128,255”)
- 小写的 “SVG 1.0 颜色名称”

## 坐标

支持以下格式的坐标位置：

- x (以像素为单位)
- p% (以容器的百分比为单位)
- p%+x, p%-x (以上两者混合)

## 全局属性

全局属性按以下格式指定：

- name1: value1
- name2: “value which may contain spaces”
- name3: \#88F
- name4: p%+x

全局属性列表：

- title-text

  指定标题文本。

- title-font

  指定标题字体。

- title-color

  指定标题文本颜色。

- desktop-image

  指定桌面背景图片。

- desktop-image-scale-method

  指定桌面背景图片缩放模式。可选值为 “stretch”, “crop”, “padding”, “fitwidth”, “fitheight”。

- desktop-image-h-align

  指定桌面背景图片水平对齐模式。可选值为 “left”, “center”, “right”。

- desktop-image-v-align

  指定桌面背景图片垂直对齐模式。可选值为 “top”, “center”, “bottom”。

- desktop-color

  指定桌面背景颜色。

- terminal-box

  指定命令行终端窗口的样式图片，例如 “terminal\_\*.png”。

- terminal-border

  指定命令行终端窗口的边框宽度。

- terminal-left

  指定命令行终端窗口的左坐标。

- terminal-top

  指定命令行终端窗口的顶部坐标。

- terminal-width

  指定命令行终端窗口的宽度。

- terminal-height

  指定命令行终端窗口的高度。

## 主题组件

在主题中创建组件，方法是在组件类型前面加上一个 “+” 号：

<div class="language-scss highlighter-rouge">

<div class="highlight">

``` highlight
+ label {
    text = "GNU GRUB 2"
    font = "Dos VGA"
    color = "#8FF"
    align = center
    preferred_size = (120, 80)
}
```

</div>

</div>

### label

该组件显示一行文字。

#### 属性

- id

  若为 “\_\_timeout\_\_” 并且没有 “text” 属性，则显示启动倒计时。

  若为 “\_\_help\_\_” 并且没有 “text” 属性，则显示选中菜单帮助文本。

  若为 “\_\_title\_\_” 并且没有 “text” 属性，则显示选中菜单标题。

- text

  要显示的文本。若文本以 “@@” 开头，则显示为变量的值，例如设为 “@@grub_cpu” 以显示变量 `grub_cpu` 的值。

- var

  要显示的变量名。与 “text” 属性不同的是，每秒刷新一次变量的值。

- hook

  与 “var” 属性搭配使用，通过执行一句简单命令来刷新变量的值。

- font

  文本的字体。

- color

  文本的颜色。

- align

  文本在组件中的水平对齐方式，选项为 “left”, “center” 和 “right”。

- visible

  设为 “false” 以隐藏此组件。

### image

该组件显示一幅图片。

#### 属性

- file

  图片的全路径。

### progress_bar

显示水平的倒计时进度条。

#### 属性

- id

  设为 “\_\_timeout\_\_” 以显示倒计时。

- fg_color

  设置前景色。

- bg_color

  设置背景色。

- border_color

  设置边框颜色。

- text_color

  设置文本颜色。

- bar_style

  设置进度条样式图片，例如 “progress_frame\_\*.png”。

- highlight_style

  设置进度条高亮区域样式图片，例如 “progress_hl\_\*.png”。

- highlight_overlay

  若设为 “true”，则高亮显示的边缘图片将覆盖进度条框架的边缘图片。默认为 “false”。

- font

  设置字体。

- text

  设置在进度条上显示的文本。若值为 “@TIMEOUT_NOTIFICATION_SHORT@”, “@TIMEOUT_NOTIFICATION_MIDDLE@” 或 “@TIMEOUT_NOTIFICATION_LONG@”，则 GRUB 将自动更新提示信息。

### circular_progress

显示圆形倒计时指示器。外观由中心图像和刻度图像决定。中心图像位于组件的中心。 围绕圆周，绘制数个刻度图像。

#### 属性

- id

  设为 “\_\_timeout\_\_” 以显示倒计时。

- center_bitmap

  设置中心图像的文件名。

- tick_bitmap

  设置刻度图像的文件名。

- num_ticks

  设置圆周上的刻度数。

- ticks_disappear

  设置当进度到达时刻度是否消失，选项为 “true” 或 “false” (默认)。

- start_angle

  设置第一个刻度标记的位置，单位为圆的 1/256。使用 “xxx deg” 或 “xxx \xc2\xb0” 可以按角度设置位置。

### boot_menu

显示 GRUB 启动菜单。

#### 属性

- item_font

  菜单项标题的字体。

- selected_item_font

  选中菜单项标题的字体。默认值为 “inherit”，继承 `item_font` 设置的字体。

- item_color

  菜单项标题的颜色。

- selected_item_color

  选中菜单项标题的颜色。默认值为 “inherit”，继承 `item_color` 设置的颜色。

- icon_width

  菜单项图标的宽度。

- icon_height

  菜单项图标的高度。

- item_height

  菜单项的高度。

- item_padding

  菜单项内容每一侧要保留的空间。

- item_icon_space

  菜单项图标与标题之间的空隙。

- item_spacing

  菜单项之间保留的空间。

- menu_pixmap_style

  启动菜单框的样式图片，例如 “menu\_\*.png”。

- item_pixmap_style

  菜单项方框的样式图片。

- selected_item_pixmap_style

  选中菜单项方框的样式图片。

- scrollbar

  是否显示滚动条，选项为 “true” 或 “false”。

- scrollbar_frame

  滚动条样式图片，如 “scrollbar\_\*.png”。

- scrollbar_thumb

  滚动条滑块的样式图片，如 “scrollbar_thumb\_\*.png”。

- scrollbar_thumb_overlay

  选项为 “true” 或 “false”。若为 “true”，则滑块侧边将会覆盖滚动条框的侧边。

- scrollbar_slice

  滚动条在菜单框架中的位置，选项为 “west”, “center”, “east” (默认)。”west” 将在左侧绘制 (右对齐)，”east” 将在右侧绘制 (左对齐)。”center” 将在中心绘制。注意：若为 “center”，如果绘制滚动条，则启动菜单项的宽度将减小滚动条的宽度，并且滚动条将绘制在中心图片的右侧。若不绘制滚动条，则菜单项的宽度为中心图片的宽度。

- scrollbar_left_pad

  滚动条左侧的空间。若 `scrollbar_slice` 为 “west”，则此属性无效。

- scrollbar_right_pad

  滚动条右侧的空间。若 `scrollbar_slice` 为 “east”，则此属性无效。

- scrollbar_top_pad

  滚动条上方的空间。

- scrollbar_bottom_pad

  滚动条底部的空间。

- visible

  设为 “false” 以隐藏启动菜单。

### canvas

`canvas` 是一个容器，可以在其内部放任何组件，且不更改其子组件的位置。

### hbox

`hbox` 容器会将其子组件从左到右排布，为每个子组件设置其首选宽度。每个子组件的高度将会设为子组件中最高的首选高度。

### vbox

`vbox` 容器会将其子组件从上到下排布，为每个子组件设置其首选高度。每个子组件的宽度将会设为子组件中最宽的首选宽度。

### animation

显示动画。需要正确地设置变量 `grub_frame_speed` 以启用动画。

<div class="language-bash highlighter-rouge">

<div class="highlight">

``` highlight
export grub_frame_speed=110
```

</div>

</div>

其中，110为帧速，单位是毫秒每帧。

#### 示例

<div class="language-scss highlighter-rouge">

<div class="highlight">

``` highlight
+ animation {
    dir_name = "IMAGE_DIR"
    image_format = png/jpg/jpeg/tga
    frame_number = n
    left = p%
    width = p%
    top = p%
    height = p%
    size_ratio = n
    start_x = n
    start_y = n
    move_speed = n
    move_direction = up/down/left/right
    play_once = pause/disappear
    hit_wall = pause/stop/disappear
    bind_menu = fixed_position/follow_single/foll_variety/full_screen
    bind_direction = left/right
}
```

</div>

</div>

#### 属性

- dirname = “IMAGE_DIR”

  动画序列图片所在文件夹，指 theme.txt 所在目录下的文件夹

- image_format = png/jpg/jpeg/tga

  指定动画图片的扩展名，支持 png, jpg, jpeg, tga

- frame_number = n

  动画序列图片总数。图片文件命名必须是数字加扩展名，数字从 1 开始，例如 1.jpg, 2.jpg, 3.jpg …

- size_ratio = n

  动画大小相对于容器的比例。

- start_x/start_y = n

  动画在容器中x/y坐标的偏移值。若动画跟随主菜单选择条目的位置而移动，则 start_y 无效。

- move_direction = up/down/left/right

  设置动画初始的移动方向，只在随机移动的动画中有效。

- move_speed = n

  设置动画移动速度。值为 0，则在原地播放动画。值不能为负数。

- play_once = pause/disappear

  设置动画播放一次后暂停显示最后一帧或消失。若不设置此项，则为循环播放。

- hit_wall = pause/stop/disappear

  设置当动画撞到容器壁后的行为，pause – 暂停播放动画，stop – 停止移动，继续播放动画，disappear – 消失。

- bind_menu = fixed_position/follow_single/foll_variety/full_screen

  设置动画作为随菜单选项变化的 logo 使用。若参数不是 follow_single，则动画序列应该放在 dir_name 参数目录的各子目录中，子目录名与 menuentry –class 的参数相同。

  - fixed_position – 在固定位置随菜单项播放动画
  - follow_single – 跟随菜单项，在不同位置显示同一组动画
  - follow_variety – 跟随菜单项，在不同位置播放不同动画
  - full_screen – 全屏显示随菜单项变化的动画
  - bind_direction = left/right 在启用跟随菜单项的动画后，设置动画的位置在主菜单左边框的左边或右边

### 通用属性

- left

  指定组件的左边框位置。

- top

  指定组件的顶部边框位置。

- width

  指定组件的宽度。

- height

  指定组件的高度。
