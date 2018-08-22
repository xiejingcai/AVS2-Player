# ESPlayer/AVS2-Player
Video Encoded Stream 播放器，可以查看每一帧视频数据，类似Elecard StreamEye的工具。
目标是支持HEVC和AVS2 ES播放和分析，目前只支持AVS2 ES。
写的比较简陋，不过可以在Linux和windows双平台编译，使用。
![image](https://github.com/xiejingcai/AVS2-Player/blob/master/ESPlayer.jpg)

# 引用项目
AVS2解码库用的是PKU开源的项目，目前10-bit解码不支持MMX指令。

https://github.com/pkuvcl/davs2

UI以及图像显示引用了SDL和SDL ttf库。

http://www.libsdl.org/

# 使用说明
1.拖拽视频文件到可执行程序图标上，或者在终端敲：ESPlayer xxx.avs2。

2.空格按键切换模式：

（1）stream play模式就是普通视频播放模式。

（2）single frame模式，可以用鼠标点击，查看每一帧信息。此模式下，可以通过鼠标中键滚动，浏览视频序列（sequence）分组（gop），鼠标左键点击选中感兴趣的分组，选择分组自动展开，左键点击查看每一帧。

3.详情请见网盘中视频演示：ESPlayer.mp4
# 测试用例
https://pan.baidu.com/s/1Enrl8TKmqfFFvXuq5wYkCQ

普通AVS2 ES ，可以单帧查看，也可以匀速播放：wild.avs2

部分AVS2码流支持不够好，只能匀速播放和暂停，后续会解决：chess.avs2，traveller.avs2



