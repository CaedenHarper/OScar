.section .text

.extern interrupt_dispatch
.extern interrupt_end_of_interrupt
.extern scheduler_interrupt_exit

.global irq32
.type irq32, @function
irq32:
    cld
    pushq $32
    jmp irq_common
.size irq32, .-irq32

.global irq33
.type irq33, @function
irq33:
    cld
    pushq $33
    jmp irq_common
.size irq33, .-irq33

.global irq34
.type irq34, @function
irq34:
    cld
    pushq $34
    jmp irq_common
.size irq34, .-irq34

.global irq35
.type irq35, @function
irq35:
    cld
    pushq $35
    jmp irq_common
.size irq35, .-irq35

.global irq36
.type irq36, @function
irq36:
    cld
    pushq $36
    jmp irq_common
.size irq36, .-irq36

.global irq37
.type irq37, @function
irq37:
    cld
    pushq $37
    jmp irq_common
.size irq37, .-irq37

.global irq38
.type irq38, @function
irq38:
    cld
    pushq $38
    jmp irq_common
.size irq38, .-irq38

.global irq39
.type irq39, @function
irq39:
    cld
    pushq $39
    jmp irq_common
.size irq39, .-irq39

.global irq40
.type irq40, @function
irq40:
    cld
    pushq $40
    jmp irq_common
.size irq40, .-irq40

.global irq41
.type irq41, @function
irq41:
    cld
    pushq $41
    jmp irq_common
.size irq41, .-irq41

.global irq42
.type irq42, @function
irq42:
    cld
    pushq $42
    jmp irq_common
.size irq42, .-irq42

.global irq43
.type irq43, @function
irq43:
    cld
    pushq $43
    jmp irq_common
.size irq43, .-irq43

.global irq44
.type irq44, @function
irq44:
    cld
    pushq $44
    jmp irq_common
.size irq44, .-irq44

.global irq45
.type irq45, @function
irq45:
    cld
    pushq $45
    jmp irq_common
.size irq45, .-irq45

.global irq46
.type irq46, @function
irq46:
    cld
    pushq $46
    jmp irq_common
.size irq46, .-irq46

.global irq47
.type irq47, @function
irq47:
    cld
    pushq $47
    jmp irq_common
.size irq47, .-irq47

.global irq48
.type irq48, @function
irq48:
    cld
    pushq $48
    jmp irq_common
.size irq48, .-irq48

.global irq49
.type irq49, @function
irq49:
    cld
    pushq $49
    jmp irq_common
.size irq49, .-irq49

.global irq50
.type irq50, @function
irq50:
    cld
    pushq $50
    jmp irq_common
.size irq50, .-irq50

.global irq51
.type irq51, @function
irq51:
    cld
    pushq $51
    jmp irq_common
.size irq51, .-irq51

.global irq52
.type irq52, @function
irq52:
    cld
    pushq $52
    jmp irq_common
.size irq52, .-irq52

.global irq53
.type irq53, @function
irq53:
    cld
    pushq $53
    jmp irq_common
.size irq53, .-irq53

.global irq54
.type irq54, @function
irq54:
    cld
    pushq $54
    jmp irq_common
.size irq54, .-irq54

.global irq55
.type irq55, @function
irq55:
    cld
    pushq $55
    jmp irq_common
.size irq55, .-irq55

.global irq56
.type irq56, @function
irq56:
    cld
    pushq $56
    jmp irq_common
.size irq56, .-irq56

.global irq57
.type irq57, @function
irq57:
    cld
    pushq $57
    jmp irq_common
.size irq57, .-irq57

.global irq58
.type irq58, @function
irq58:
    cld
    pushq $58
    jmp irq_common
.size irq58, .-irq58

.global irq59
.type irq59, @function
irq59:
    cld
    pushq $59
    jmp irq_common
.size irq59, .-irq59

.global irq60
.type irq60, @function
irq60:
    cld
    pushq $60
    jmp irq_common
.size irq60, .-irq60

.global irq61
.type irq61, @function
irq61:
    cld
    pushq $61
    jmp irq_common
.size irq61, .-irq61

.global irq62
.type irq62, @function
irq62:
    cld
    pushq $62
    jmp irq_common
.size irq62, .-irq62

.global irq63
.type irq63, @function
irq63:
    cld
    pushq $63
    jmp irq_common
.size irq63, .-irq63

.global irq64
.type irq64, @function
irq64:
    cld
    pushq $64
    jmp irq_common
.size irq64, .-irq64

.global irq65
.type irq65, @function
irq65:
    cld
    pushq $65
    jmp irq_common
.size irq65, .-irq65

.global irq66
.type irq66, @function
irq66:
    cld
    pushq $66
    jmp irq_common
.size irq66, .-irq66

.global irq67
.type irq67, @function
irq67:
    cld
    pushq $67
    jmp irq_common
.size irq67, .-irq67

.global irq68
.type irq68, @function
irq68:
    cld
    pushq $68
    jmp irq_common
.size irq68, .-irq68

.global irq69
.type irq69, @function
irq69:
    cld
    pushq $69
    jmp irq_common
.size irq69, .-irq69

.global irq70
.type irq70, @function
irq70:
    cld
    pushq $70
    jmp irq_common
.size irq70, .-irq70

.global irq71
.type irq71, @function
irq71:
    cld
    pushq $71
    jmp irq_common
.size irq71, .-irq71

.global irq72
.type irq72, @function
irq72:
    cld
    pushq $72
    jmp irq_common
.size irq72, .-irq72

.global irq73
.type irq73, @function
irq73:
    cld
    pushq $73
    jmp irq_common
.size irq73, .-irq73

.global irq74
.type irq74, @function
irq74:
    cld
    pushq $74
    jmp irq_common
.size irq74, .-irq74

.global irq75
.type irq75, @function
irq75:
    cld
    pushq $75
    jmp irq_common
.size irq75, .-irq75

.global irq76
.type irq76, @function
irq76:
    cld
    pushq $76
    jmp irq_common
.size irq76, .-irq76

.global irq77
.type irq77, @function
irq77:
    cld
    pushq $77
    jmp irq_common
.size irq77, .-irq77

.global irq78
.type irq78, @function
irq78:
    cld
    pushq $78
    jmp irq_common
.size irq78, .-irq78

.global irq79
.type irq79, @function
irq79:
    cld
    pushq $79
    jmp irq_common
.size irq79, .-irq79

.global irq80
.type irq80, @function
irq80:
    cld
    pushq $80
    jmp irq_common
.size irq80, .-irq80

.global irq81
.type irq81, @function
irq81:
    cld
    pushq $81
    jmp irq_common
.size irq81, .-irq81

.global irq82
.type irq82, @function
irq82:
    cld
    pushq $82
    jmp irq_common
.size irq82, .-irq82

.global irq83
.type irq83, @function
irq83:
    cld
    pushq $83
    jmp irq_common
.size irq83, .-irq83

.global irq84
.type irq84, @function
irq84:
    cld
    pushq $84
    jmp irq_common
.size irq84, .-irq84

.global irq85
.type irq85, @function
irq85:
    cld
    pushq $85
    jmp irq_common
.size irq85, .-irq85

.global irq86
.type irq86, @function
irq86:
    cld
    pushq $86
    jmp irq_common
.size irq86, .-irq86

.global irq87
.type irq87, @function
irq87:
    cld
    pushq $87
    jmp irq_common
.size irq87, .-irq87

.global irq88
.type irq88, @function
irq88:
    cld
    pushq $88
    jmp irq_common
.size irq88, .-irq88

.global irq89
.type irq89, @function
irq89:
    cld
    pushq $89
    jmp irq_common
.size irq89, .-irq89

.global irq90
.type irq90, @function
irq90:
    cld
    pushq $90
    jmp irq_common
.size irq90, .-irq90

.global irq91
.type irq91, @function
irq91:
    cld
    pushq $91
    jmp irq_common
.size irq91, .-irq91

.global irq92
.type irq92, @function
irq92:
    cld
    pushq $92
    jmp irq_common
.size irq92, .-irq92

.global irq93
.type irq93, @function
irq93:
    cld
    pushq $93
    jmp irq_common
.size irq93, .-irq93

.global irq94
.type irq94, @function
irq94:
    cld
    pushq $94
    jmp irq_common
.size irq94, .-irq94

.global irq95
.type irq95, @function
irq95:
    cld
    pushq $95
    jmp irq_common
.size irq95, .-irq95

.global irq96
.type irq96, @function
irq96:
    cld
    pushq $96
    jmp irq_common
.size irq96, .-irq96

.global irq97
.type irq97, @function
irq97:
    cld
    pushq $97
    jmp irq_common
.size irq97, .-irq97

.global irq98
.type irq98, @function
irq98:
    cld
    pushq $98
    jmp irq_common
.size irq98, .-irq98

.global irq99
.type irq99, @function
irq99:
    cld
    pushq $99
    jmp irq_common
.size irq99, .-irq99

.global irq100
.type irq100, @function
irq100:
    cld
    pushq $100
    jmp irq_common
.size irq100, .-irq100

.global irq101
.type irq101, @function
irq101:
    cld
    pushq $101
    jmp irq_common
.size irq101, .-irq101

.global irq102
.type irq102, @function
irq102:
    cld
    pushq $102
    jmp irq_common
.size irq102, .-irq102

.global irq103
.type irq103, @function
irq103:
    cld
    pushq $103
    jmp irq_common
.size irq103, .-irq103

.global irq104
.type irq104, @function
irq104:
    cld
    pushq $104
    jmp irq_common
.size irq104, .-irq104

.global irq105
.type irq105, @function
irq105:
    cld
    pushq $105
    jmp irq_common
.size irq105, .-irq105

.global irq106
.type irq106, @function
irq106:
    cld
    pushq $106
    jmp irq_common
.size irq106, .-irq106

.global irq107
.type irq107, @function
irq107:
    cld
    pushq $107
    jmp irq_common
.size irq107, .-irq107

.global irq108
.type irq108, @function
irq108:
    cld
    pushq $108
    jmp irq_common
.size irq108, .-irq108

.global irq109
.type irq109, @function
irq109:
    cld
    pushq $109
    jmp irq_common
.size irq109, .-irq109

.global irq110
.type irq110, @function
irq110:
    cld
    pushq $110
    jmp irq_common
.size irq110, .-irq110

.global irq111
.type irq111, @function
irq111:
    cld
    pushq $111
    jmp irq_common
.size irq111, .-irq111

.global irq112
.type irq112, @function
irq112:
    cld
    pushq $112
    jmp irq_common
.size irq112, .-irq112

.global irq113
.type irq113, @function
irq113:
    cld
    pushq $113
    jmp irq_common
.size irq113, .-irq113

.global irq114
.type irq114, @function
irq114:
    cld
    pushq $114
    jmp irq_common
.size irq114, .-irq114

.global irq115
.type irq115, @function
irq115:
    cld
    pushq $115
    jmp irq_common
.size irq115, .-irq115

.global irq116
.type irq116, @function
irq116:
    cld
    pushq $116
    jmp irq_common
.size irq116, .-irq116

.global irq117
.type irq117, @function
irq117:
    cld
    pushq $117
    jmp irq_common
.size irq117, .-irq117

.global irq118
.type irq118, @function
irq118:
    cld
    pushq $118
    jmp irq_common
.size irq118, .-irq118

.global irq119
.type irq119, @function
irq119:
    cld
    pushq $119
    jmp irq_common
.size irq119, .-irq119

.global irq120
.type irq120, @function
irq120:
    cld
    pushq $120
    jmp irq_common
.size irq120, .-irq120

.global irq121
.type irq121, @function
irq121:
    cld
    pushq $121
    jmp irq_common
.size irq121, .-irq121

.global irq122
.type irq122, @function
irq122:
    cld
    pushq $122
    jmp irq_common
.size irq122, .-irq122

.global irq123
.type irq123, @function
irq123:
    cld
    pushq $123
    jmp irq_common
.size irq123, .-irq123

.global irq124
.type irq124, @function
irq124:
    cld
    pushq $124
    jmp irq_common
.size irq124, .-irq124

.global irq125
.type irq125, @function
irq125:
    cld
    pushq $125
    jmp irq_common
.size irq125, .-irq125

.global irq126
.type irq126, @function
irq126:
    cld
    pushq $126
    jmp irq_common
.size irq126, .-irq126

.global irq127
.type irq127, @function
irq127:
    cld
    pushq $127
    jmp irq_common
.size irq127, .-irq127

.global irq128
.type irq128, @function
irq128:
    cld
    pushq $128
    jmp irq_common
.size irq128, .-irq128

.global irq129
.type irq129, @function
irq129:
    cld
    pushq $129
    jmp irq_common
.size irq129, .-irq129

.global irq130
.type irq130, @function
irq130:
    cld
    pushq $130
    jmp irq_common
.size irq130, .-irq130

.global irq131
.type irq131, @function
irq131:
    cld
    pushq $131
    jmp irq_common
.size irq131, .-irq131

.global irq132
.type irq132, @function
irq132:
    cld
    pushq $132
    jmp irq_common
.size irq132, .-irq132

.global irq133
.type irq133, @function
irq133:
    cld
    pushq $133
    jmp irq_common
.size irq133, .-irq133

.global irq134
.type irq134, @function
irq134:
    cld
    pushq $134
    jmp irq_common
.size irq134, .-irq134

.global irq135
.type irq135, @function
irq135:
    cld
    pushq $135
    jmp irq_common
.size irq135, .-irq135

.global irq136
.type irq136, @function
irq136:
    cld
    pushq $136
    jmp irq_common
.size irq136, .-irq136

.global irq137
.type irq137, @function
irq137:
    cld
    pushq $137
    jmp irq_common
.size irq137, .-irq137

.global irq138
.type irq138, @function
irq138:
    cld
    pushq $138
    jmp irq_common
.size irq138, .-irq138

.global irq139
.type irq139, @function
irq139:
    cld
    pushq $139
    jmp irq_common
.size irq139, .-irq139

.global irq140
.type irq140, @function
irq140:
    cld
    pushq $140
    jmp irq_common
.size irq140, .-irq140

.global irq141
.type irq141, @function
irq141:
    cld
    pushq $141
    jmp irq_common
.size irq141, .-irq141

.global irq142
.type irq142, @function
irq142:
    cld
    pushq $142
    jmp irq_common
.size irq142, .-irq142

.global irq143
.type irq143, @function
irq143:
    cld
    pushq $143
    jmp irq_common
.size irq143, .-irq143

.global irq144
.type irq144, @function
irq144:
    cld
    pushq $144
    jmp irq_common
.size irq144, .-irq144

.global irq145
.type irq145, @function
irq145:
    cld
    pushq $145
    jmp irq_common
.size irq145, .-irq145

.global irq146
.type irq146, @function
irq146:
    cld
    pushq $146
    jmp irq_common
.size irq146, .-irq146

.global irq147
.type irq147, @function
irq147:
    cld
    pushq $147
    jmp irq_common
.size irq147, .-irq147

.global irq148
.type irq148, @function
irq148:
    cld
    pushq $148
    jmp irq_common
.size irq148, .-irq148

.global irq149
.type irq149, @function
irq149:
    cld
    pushq $149
    jmp irq_common
.size irq149, .-irq149

.global irq150
.type irq150, @function
irq150:
    cld
    pushq $150
    jmp irq_common
.size irq150, .-irq150

.global irq151
.type irq151, @function
irq151:
    cld
    pushq $151
    jmp irq_common
.size irq151, .-irq151

.global irq152
.type irq152, @function
irq152:
    cld
    pushq $152
    jmp irq_common
.size irq152, .-irq152

.global irq153
.type irq153, @function
irq153:
    cld
    pushq $153
    jmp irq_common
.size irq153, .-irq153

.global irq154
.type irq154, @function
irq154:
    cld
    pushq $154
    jmp irq_common
.size irq154, .-irq154

.global irq155
.type irq155, @function
irq155:
    cld
    pushq $155
    jmp irq_common
.size irq155, .-irq155

.global irq156
.type irq156, @function
irq156:
    cld
    pushq $156
    jmp irq_common
.size irq156, .-irq156

.global irq157
.type irq157, @function
irq157:
    cld
    pushq $157
    jmp irq_common
.size irq157, .-irq157

.global irq158
.type irq158, @function
irq158:
    cld
    pushq $158
    jmp irq_common
.size irq158, .-irq158

.global irq159
.type irq159, @function
irq159:
    cld
    pushq $159
    jmp irq_common
.size irq159, .-irq159

.global irq160
.type irq160, @function
irq160:
    cld
    pushq $160
    jmp irq_common
.size irq160, .-irq160

.global irq161
.type irq161, @function
irq161:
    cld
    pushq $161
    jmp irq_common
.size irq161, .-irq161

.global irq162
.type irq162, @function
irq162:
    cld
    pushq $162
    jmp irq_common
.size irq162, .-irq162

.global irq163
.type irq163, @function
irq163:
    cld
    pushq $163
    jmp irq_common
.size irq163, .-irq163

.global irq164
.type irq164, @function
irq164:
    cld
    pushq $164
    jmp irq_common
.size irq164, .-irq164

.global irq165
.type irq165, @function
irq165:
    cld
    pushq $165
    jmp irq_common
.size irq165, .-irq165

.global irq166
.type irq166, @function
irq166:
    cld
    pushq $166
    jmp irq_common
.size irq166, .-irq166

.global irq167
.type irq167, @function
irq167:
    cld
    pushq $167
    jmp irq_common
.size irq167, .-irq167

.global irq168
.type irq168, @function
irq168:
    cld
    pushq $168
    jmp irq_common
.size irq168, .-irq168

.global irq169
.type irq169, @function
irq169:
    cld
    pushq $169
    jmp irq_common
.size irq169, .-irq169

.global irq170
.type irq170, @function
irq170:
    cld
    pushq $170
    jmp irq_common
.size irq170, .-irq170

.global irq171
.type irq171, @function
irq171:
    cld
    pushq $171
    jmp irq_common
.size irq171, .-irq171

.global irq172
.type irq172, @function
irq172:
    cld
    pushq $172
    jmp irq_common
.size irq172, .-irq172

.global irq173
.type irq173, @function
irq173:
    cld
    pushq $173
    jmp irq_common
.size irq173, .-irq173

.global irq174
.type irq174, @function
irq174:
    cld
    pushq $174
    jmp irq_common
.size irq174, .-irq174

.global irq175
.type irq175, @function
irq175:
    cld
    pushq $175
    jmp irq_common
.size irq175, .-irq175

.global irq176
.type irq176, @function
irq176:
    cld
    pushq $176
    jmp irq_common
.size irq176, .-irq176

.global irq177
.type irq177, @function
irq177:
    cld
    pushq $177
    jmp irq_common
.size irq177, .-irq177

.global irq178
.type irq178, @function
irq178:
    cld
    pushq $178
    jmp irq_common
.size irq178, .-irq178

.global irq179
.type irq179, @function
irq179:
    cld
    pushq $179
    jmp irq_common
.size irq179, .-irq179

.global irq180
.type irq180, @function
irq180:
    cld
    pushq $180
    jmp irq_common
.size irq180, .-irq180

.global irq181
.type irq181, @function
irq181:
    cld
    pushq $181
    jmp irq_common
.size irq181, .-irq181

.global irq182
.type irq182, @function
irq182:
    cld
    pushq $182
    jmp irq_common
.size irq182, .-irq182

.global irq183
.type irq183, @function
irq183:
    cld
    pushq $183
    jmp irq_common
.size irq183, .-irq183

.global irq184
.type irq184, @function
irq184:
    cld
    pushq $184
    jmp irq_common
.size irq184, .-irq184

.global irq185
.type irq185, @function
irq185:
    cld
    pushq $185
    jmp irq_common
.size irq185, .-irq185

.global irq186
.type irq186, @function
irq186:
    cld
    pushq $186
    jmp irq_common
.size irq186, .-irq186

.global irq187
.type irq187, @function
irq187:
    cld
    pushq $187
    jmp irq_common
.size irq187, .-irq187

.global irq188
.type irq188, @function
irq188:
    cld
    pushq $188
    jmp irq_common
.size irq188, .-irq188

.global irq189
.type irq189, @function
irq189:
    cld
    pushq $189
    jmp irq_common
.size irq189, .-irq189

.global irq190
.type irq190, @function
irq190:
    cld
    pushq $190
    jmp irq_common
.size irq190, .-irq190

.global irq191
.type irq191, @function
irq191:
    cld
    pushq $191
    jmp irq_common
.size irq191, .-irq191

.global irq192
.type irq192, @function
irq192:
    cld
    pushq $192
    jmp irq_common
.size irq192, .-irq192

.global irq193
.type irq193, @function
irq193:
    cld
    pushq $193
    jmp irq_common
.size irq193, .-irq193

.global irq194
.type irq194, @function
irq194:
    cld
    pushq $194
    jmp irq_common
.size irq194, .-irq194

.global irq195
.type irq195, @function
irq195:
    cld
    pushq $195
    jmp irq_common
.size irq195, .-irq195

.global irq196
.type irq196, @function
irq196:
    cld
    pushq $196
    jmp irq_common
.size irq196, .-irq196

.global irq197
.type irq197, @function
irq197:
    cld
    pushq $197
    jmp irq_common
.size irq197, .-irq197

.global irq198
.type irq198, @function
irq198:
    cld
    pushq $198
    jmp irq_common
.size irq198, .-irq198

.global irq199
.type irq199, @function
irq199:
    cld
    pushq $199
    jmp irq_common
.size irq199, .-irq199

.global irq200
.type irq200, @function
irq200:
    cld
    pushq $200
    jmp irq_common
.size irq200, .-irq200

.global irq201
.type irq201, @function
irq201:
    cld
    pushq $201
    jmp irq_common
.size irq201, .-irq201

.global irq202
.type irq202, @function
irq202:
    cld
    pushq $202
    jmp irq_common
.size irq202, .-irq202

.global irq203
.type irq203, @function
irq203:
    cld
    pushq $203
    jmp irq_common
.size irq203, .-irq203

.global irq204
.type irq204, @function
irq204:
    cld
    pushq $204
    jmp irq_common
.size irq204, .-irq204

.global irq205
.type irq205, @function
irq205:
    cld
    pushq $205
    jmp irq_common
.size irq205, .-irq205

.global irq206
.type irq206, @function
irq206:
    cld
    pushq $206
    jmp irq_common
.size irq206, .-irq206

.global irq207
.type irq207, @function
irq207:
    cld
    pushq $207
    jmp irq_common
.size irq207, .-irq207

.global irq208
.type irq208, @function
irq208:
    cld
    pushq $208
    jmp irq_common
.size irq208, .-irq208

.global irq209
.type irq209, @function
irq209:
    cld
    pushq $209
    jmp irq_common
.size irq209, .-irq209

.global irq210
.type irq210, @function
irq210:
    cld
    pushq $210
    jmp irq_common
.size irq210, .-irq210

.global irq211
.type irq211, @function
irq211:
    cld
    pushq $211
    jmp irq_common
.size irq211, .-irq211

.global irq212
.type irq212, @function
irq212:
    cld
    pushq $212
    jmp irq_common
.size irq212, .-irq212

.global irq213
.type irq213, @function
irq213:
    cld
    pushq $213
    jmp irq_common
.size irq213, .-irq213

.global irq214
.type irq214, @function
irq214:
    cld
    pushq $214
    jmp irq_common
.size irq214, .-irq214

.global irq215
.type irq215, @function
irq215:
    cld
    pushq $215
    jmp irq_common
.size irq215, .-irq215

.global irq216
.type irq216, @function
irq216:
    cld
    pushq $216
    jmp irq_common
.size irq216, .-irq216

.global irq217
.type irq217, @function
irq217:
    cld
    pushq $217
    jmp irq_common
.size irq217, .-irq217

.global irq218
.type irq218, @function
irq218:
    cld
    pushq $218
    jmp irq_common
.size irq218, .-irq218

.global irq219
.type irq219, @function
irq219:
    cld
    pushq $219
    jmp irq_common
.size irq219, .-irq219

.global irq220
.type irq220, @function
irq220:
    cld
    pushq $220
    jmp irq_common
.size irq220, .-irq220

.global irq221
.type irq221, @function
irq221:
    cld
    pushq $221
    jmp irq_common
.size irq221, .-irq221

.global irq222
.type irq222, @function
irq222:
    cld
    pushq $222
    jmp irq_common
.size irq222, .-irq222

.global irq223
.type irq223, @function
irq223:
    cld
    pushq $223
    jmp irq_common
.size irq223, .-irq223

.global irq224
.type irq224, @function
irq224:
    cld
    pushq $224
    jmp irq_common
.size irq224, .-irq224

.global irq225
.type irq225, @function
irq225:
    cld
    pushq $225
    jmp irq_common
.size irq225, .-irq225

.global irq226
.type irq226, @function
irq226:
    cld
    pushq $226
    jmp irq_common
.size irq226, .-irq226

.global irq227
.type irq227, @function
irq227:
    cld
    pushq $227
    jmp irq_common
.size irq227, .-irq227

.global irq228
.type irq228, @function
irq228:
    cld
    pushq $228
    jmp irq_common
.size irq228, .-irq228

.global irq229
.type irq229, @function
irq229:
    cld
    pushq $229
    jmp irq_common
.size irq229, .-irq229

.global irq230
.type irq230, @function
irq230:
    cld
    pushq $230
    jmp irq_common
.size irq230, .-irq230

.global irq231
.type irq231, @function
irq231:
    cld
    pushq $231
    jmp irq_common
.size irq231, .-irq231

.global irq232
.type irq232, @function
irq232:
    cld
    pushq $232
    jmp irq_common
.size irq232, .-irq232

.global irq233
.type irq233, @function
irq233:
    cld
    pushq $233
    jmp irq_common
.size irq233, .-irq233

.global irq234
.type irq234, @function
irq234:
    cld
    pushq $234
    jmp irq_common
.size irq234, .-irq234

.global irq235
.type irq235, @function
irq235:
    cld
    pushq $235
    jmp irq_common
.size irq235, .-irq235

.global irq236
.type irq236, @function
irq236:
    cld
    pushq $236
    jmp irq_common
.size irq236, .-irq236

.global irq237
.type irq237, @function
irq237:
    cld
    pushq $237
    jmp irq_common
.size irq237, .-irq237

.global irq238
.type irq238, @function
irq238:
    cld
    pushq $238
    jmp irq_common
.size irq238, .-irq238

.global irq239
.type irq239, @function
irq239:
    cld
    pushq $239
    jmp irq_common
.size irq239, .-irq239

.global irq240
.type irq240, @function
irq240:
    cld
    pushq $240
    jmp irq_common
.size irq240, .-irq240

.global irq241
.type irq241, @function
irq241:
    cld
    pushq $241
    jmp irq_common
.size irq241, .-irq241

.global irq242
.type irq242, @function
irq242:
    cld
    pushq $242
    jmp irq_common
.size irq242, .-irq242

.global irq243
.type irq243, @function
irq243:
    cld
    pushq $243
    jmp irq_common
.size irq243, .-irq243

.global irq244
.type irq244, @function
irq244:
    cld
    pushq $244
    jmp irq_common
.size irq244, .-irq244

.global irq245
.type irq245, @function
irq245:
    cld
    pushq $245
    jmp irq_common
.size irq245, .-irq245

.global irq246
.type irq246, @function
irq246:
    cld
    pushq $246
    jmp irq_common
.size irq246, .-irq246

.global irq247
.type irq247, @function
irq247:
    cld
    pushq $247
    jmp irq_common
.size irq247, .-irq247

.global irq248
.type irq248, @function
irq248:
    cld
    pushq $248
    jmp irq_common
.size irq248, .-irq248

.global irq249
.type irq249, @function
irq249:
    cld
    pushq $249
    jmp irq_common
.size irq249, .-irq249

.global irq250
.type irq250, @function
irq250:
    cld
    pushq $250
    jmp irq_common
.size irq250, .-irq250

.global irq251
.type irq251, @function
irq251:
    cld
    pushq $251
    jmp irq_common
.size irq251, .-irq251

.global irq252
.type irq252, @function
irq252:
    cld
    pushq $252
    jmp irq_common
.size irq252, .-irq252

.global irq253
.type irq253, @function
irq253:
    cld
    pushq $253
    jmp irq_common
.size irq253, .-irq253

.global irq254
.type irq254, @function
irq254:
    cld
    pushq $254
    jmp irq_common
.size irq254, .-irq254

.type irq_common, @function
irq_common:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    // Fifteen saved registers precede the vector pushed by the trampoline.
    movq 120(%rsp), %rdi
    call interrupt_dispatch
    movq 120(%rsp), %rdi
    call interrupt_end_of_interrupt
    call scheduler_interrupt_exit

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $8, %rsp
    iretq
.size irq_common, .-irq_common

.section .rodata
.global irq_stub_table
.type irq_stub_table, @object
irq_stub_table:
    .quad irq32
    .quad irq33
    .quad irq34
    .quad irq35
    .quad irq36
    .quad irq37
    .quad irq38
    .quad irq39
    .quad irq40
    .quad irq41
    .quad irq42
    .quad irq43
    .quad irq44
    .quad irq45
    .quad irq46
    .quad irq47
    .quad irq48
    .quad irq49
    .quad irq50
    .quad irq51
    .quad irq52
    .quad irq53
    .quad irq54
    .quad irq55
    .quad irq56
    .quad irq57
    .quad irq58
    .quad irq59
    .quad irq60
    .quad irq61
    .quad irq62
    .quad irq63
    .quad irq64
    .quad irq65
    .quad irq66
    .quad irq67
    .quad irq68
    .quad irq69
    .quad irq70
    .quad irq71
    .quad irq72
    .quad irq73
    .quad irq74
    .quad irq75
    .quad irq76
    .quad irq77
    .quad irq78
    .quad irq79
    .quad irq80
    .quad irq81
    .quad irq82
    .quad irq83
    .quad irq84
    .quad irq85
    .quad irq86
    .quad irq87
    .quad irq88
    .quad irq89
    .quad irq90
    .quad irq91
    .quad irq92
    .quad irq93
    .quad irq94
    .quad irq95
    .quad irq96
    .quad irq97
    .quad irq98
    .quad irq99
    .quad irq100
    .quad irq101
    .quad irq102
    .quad irq103
    .quad irq104
    .quad irq105
    .quad irq106
    .quad irq107
    .quad irq108
    .quad irq109
    .quad irq110
    .quad irq111
    .quad irq112
    .quad irq113
    .quad irq114
    .quad irq115
    .quad irq116
    .quad irq117
    .quad irq118
    .quad irq119
    .quad irq120
    .quad irq121
    .quad irq122
    .quad irq123
    .quad irq124
    .quad irq125
    .quad irq126
    .quad irq127
    .quad irq128
    .quad irq129
    .quad irq130
    .quad irq131
    .quad irq132
    .quad irq133
    .quad irq134
    .quad irq135
    .quad irq136
    .quad irq137
    .quad irq138
    .quad irq139
    .quad irq140
    .quad irq141
    .quad irq142
    .quad irq143
    .quad irq144
    .quad irq145
    .quad irq146
    .quad irq147
    .quad irq148
    .quad irq149
    .quad irq150
    .quad irq151
    .quad irq152
    .quad irq153
    .quad irq154
    .quad irq155
    .quad irq156
    .quad irq157
    .quad irq158
    .quad irq159
    .quad irq160
    .quad irq161
    .quad irq162
    .quad irq163
    .quad irq164
    .quad irq165
    .quad irq166
    .quad irq167
    .quad irq168
    .quad irq169
    .quad irq170
    .quad irq171
    .quad irq172
    .quad irq173
    .quad irq174
    .quad irq175
    .quad irq176
    .quad irq177
    .quad irq178
    .quad irq179
    .quad irq180
    .quad irq181
    .quad irq182
    .quad irq183
    .quad irq184
    .quad irq185
    .quad irq186
    .quad irq187
    .quad irq188
    .quad irq189
    .quad irq190
    .quad irq191
    .quad irq192
    .quad irq193
    .quad irq194
    .quad irq195
    .quad irq196
    .quad irq197
    .quad irq198
    .quad irq199
    .quad irq200
    .quad irq201
    .quad irq202
    .quad irq203
    .quad irq204
    .quad irq205
    .quad irq206
    .quad irq207
    .quad irq208
    .quad irq209
    .quad irq210
    .quad irq211
    .quad irq212
    .quad irq213
    .quad irq214
    .quad irq215
    .quad irq216
    .quad irq217
    .quad irq218
    .quad irq219
    .quad irq220
    .quad irq221
    .quad irq222
    .quad irq223
    .quad irq224
    .quad irq225
    .quad irq226
    .quad irq227
    .quad irq228
    .quad irq229
    .quad irq230
    .quad irq231
    .quad irq232
    .quad irq233
    .quad irq234
    .quad irq235
    .quad irq236
    .quad irq237
    .quad irq238
    .quad irq239
    .quad irq240
    .quad irq241
    .quad irq242
    .quad irq243
    .quad irq244
    .quad irq245
    .quad irq246
    .quad irq247
    .quad irq248
    .quad irq249
    .quad irq250
    .quad irq251
    .quad irq252
    .quad irq253
    .quad irq254
.size irq_stub_table, .-irq_stub_table
