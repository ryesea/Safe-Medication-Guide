/*
 * page_generator.c
 * Co-Co 안전 복약 가이드 - 웹사이트 자동 생성기
 *
 * 목적: 외국인·청각장애인·시각장애인이 약 복용 정보를 이해하는 데
 *       겪는 격차를 줄여 오남용을 예방한다.
 *
 * 구조 (3단계 중 2·3단계를 이 프로그램이 만든다):
 *   1) 픽토그램 : 포장에 직접 인쇄 (이 프로그램 범위 밖)
 *   2) 입구 2개 : NFC(옆에 점자 표시, 터치만 하면 됨 -> 시각장애인 포함)
 *                 / QR(카메라로 찍어야 함 -> 외국인·일반인용)
 *   3) 웹사이트 : 언어변환 글(외국인용) + 수어·음성 영상(청각·시각장애인용)
 *
 * 이 프로그램이 만드는 파일:
 *   - index.html   : Co-Co 메인 홈페이지 (로고/메뉴바/소개 + 약 목록)
 *   - 약품ID.html  : 약품별 상세 페이지 (영상 화면 / 글 화면 전환)
 *
 * 입구 분기: 상세 페이지 주소 뒤에 ?view=video 또는 ?view=text 를 붙이면
 *            NFC(영상·음성 우선) / QR(글 우선) 로 서로 다른 화면이 먼저 뜬다.
 *
 * 글 화면의 구성 (약 1개당):
 *   1. 복용법
 *   2. 복용 금지 대상  (1) 복용하지 말 것  (2) 의사·약사와 상의  (3) 즉각 중지 후 상의
 *   3. 주의 사항
 *   -> 항목이 하나도 없는 칸은 화면에서 자동으로 빠진다.
 *
 * drugs.txt 형식 (약마다 아래 블록을 반복, 약 사이에는 '#id' 로 구분):
 *   #id tylenol                  <- 파일 이름용 영문 ID (약의 시작)
 *   #name_ko 타이레놀정 500mg
 *   #name_en Tylenol Tablet 500mg
 *   #ingredient 주성분: 아세트아미노펜 500mg   (없으면 생략 가능)
 *   #ingredient_en Acetaminophen 500 mg        (한국어 외 언어에 표시, 영문 성분명)
 *   #video https://www.youtube.com/embed/영상ID
 *   #dose_ko                     <- 이 줄 아래에 "- 항목" 을 한 줄에 하나씩
 *   - 만 12세 이상: 1회 1~2정 ...
 *   #dose_en / #ban_ko / #ban_en / #consult_ko / #consult_en
 *   #stop_ko / #stop_en / #caution_ko / #caution_en  도 같은 방식
 *   언어는 ko(한국어) en(영어) zh(중국어 간체) vi(베트남어) th(태국어) 5개.
 *   약 이름은 한국어 외에는 name_en 을 같이 쓴다.
 *   (ban=복용하지 말 것, consult=상의, stop=즉각 중지)
 *   '#' 로 시작하는 줄 중 위 이름이 아닌 것과, 빈 줄은 무시한다.
 *
 * 컴파일: gcc page_generator.c -o generator
 * 실행:   ./generator
 */

#include <stdio.h>
#include <string.h>

#define MAX_DRUGS 50
#define MAX_LIST  4096   /* 한 칸(예: 복용 금지 대상 한국어 목록)의 최대 글자 수(바이트) */
#define MAX_LINE  4096

/* 칸 종류와 언어 */
enum { S_DOSE, S_BAN, S_CONSULT, S_STOP, S_CAUTION, N_SEC };
enum { L_KO, L_EN, L_ZH, L_VI, L_TH, N_LANG };

static const char *SEC_KEY[N_SEC] = { "dose", "ban", "consult", "stop", "caution" };
static const char *LANG_KEY[N_LANG] = { "ko", "en", "zh", "vi", "th" };
/* 성분 줄 앞에 붙는 말 (한국어는 drugs.txt 의 #ingredient 에 "주성분: " 까지 직접 적는다) */
static const char *ING_LABEL[N_LANG] = { "", "Active ingredients: ", "主要成分：", "Thành phần chính: ", "ตัวยาสำคัญ: " };

/* 약품 1개의 정보를 담는 구조체 */
typedef struct {
    char id[64];            /* 파일 이름용 영문 ID (예: tylenol) */
    char name_ko[256];      /* 약품명 (한국어) */
    char name_en[256];      /* 약품명 (영어) */
    char ingredient[512];   /* 주성분 (한국어, 없으면 빈 문자열) */
    char ingredient_en[512];/* 주성분 (영문 성분명, 한국어 외 모든 언어에서 사용) */
    char video_url[512];    /* 수어+음성 안내 영상 주소 */
    /* 칸마다 항목들을 '\x1f'(구분 문자)로 이어 붙여 저장한다. */
    char list[N_LANG][N_SEC][MAX_LIST];
} Drug;

static Drug drugs[MAX_DRUGS];   /* 크기가 커서 전역으로 둔다 */

/* 줄 끝의 공백·개행 제거 */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

/* "#키 값" 한 줄의 값 부분을 dst에 복사 */
static void copy_value(char *dst, size_t cap, const char *val)
{
    while (*val == ' ' || *val == '\t') val++;
    if (strlen(val) >= cap) printf("[경고] 값이 너무 길어 잘렸습니다: %.30s...\n", val);
    strncpy(dst, val, cap - 1);
    dst[cap - 1] = '\0';
}

/* "#dose_ko" 같은 키를 칸 번호로 바꾼다. 맞는 게 없으면 0 */
static int find_list_key(const char *key, int *lang, int *sec)
{
    for (int s = 0; s < N_SEC; s++)
        for (int l = 0; l < N_LANG; l++) {
            char name[32];
            snprintf(name, sizeof(name), "%s_%s", SEC_KEY[s], LANG_KEY[l]);
            if (strcmp(key, name) == 0) { *lang = l; *sec = s; return 1; }
        }
    return 0;
}

/* drugs.txt 전체를 읽어 약 목록을 만든다. 읽은 약 개수를 돌려준다. */
static int load_drugs(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return -1;

    char line[MAX_LINE];
    int n = 0;
    int cur_lang = -1, cur_sec = -1;   /* 지금 항목을 모으는 중인 칸 */

    while (fgets(line, sizeof(line), fp) != NULL) {
        rtrim(line);
        if (line[0] == '\0') continue;

        if (line[0] == '#') {
            char key[64] = "";
            const char *val = "";
            char *sp = strpbrk(line + 1, " \t");
            if (sp != NULL) { *sp = '\0'; val = sp + 1; }
            snprintf(key, sizeof(key), "%s", line + 1);

            cur_lang = cur_sec = -1;
            if (strcmp(key, "id") == 0) {
                if (n >= MAX_DRUGS) { printf("[경고] 약은 최대 %d개까지만 읽습니다.\n", MAX_DRUGS); break; }
                n++;
                memset(&drugs[n-1], 0, sizeof(Drug));
                copy_value(drugs[n-1].id, sizeof(drugs[n-1].id), val);
            } else if (n == 0) {
                continue;   /* #id 앞에 나온 줄은 무시 */
            } else if (strcmp(key, "name_ko") == 0)    copy_value(drugs[n-1].name_ko,    sizeof(drugs[n-1].name_ko),    val);
            else if (strcmp(key, "name_en") == 0)      copy_value(drugs[n-1].name_en,    sizeof(drugs[n-1].name_en),    val);
            else if (strcmp(key, "ingredient") == 0)   copy_value(drugs[n-1].ingredient, sizeof(drugs[n-1].ingredient), val);
            else if (strcmp(key, "ingredient_en") == 0) copy_value(drugs[n-1].ingredient_en, sizeof(drugs[n-1].ingredient_en), val);
            else if (strcmp(key, "video") == 0)        copy_value(drugs[n-1].video_url,  sizeof(drugs[n-1].video_url),  val);
            else {
                int l, s;
                if (find_list_key(key, &l, &s)) { cur_lang = l; cur_sec = s; }
            }
            continue;
        }

        /* "- 항목" 줄: 현재 칸에 붙인다 */
        if (n > 0 && cur_sec >= 0 && line[0] == '-') {
            const char *item = line + 1;
            while (*item == ' ') item++;
            if (*item == '\0') continue;
            char *dst = drugs[n-1].list[cur_lang][cur_sec];
            size_t used = strlen(dst);
            if (used + strlen(item) + 2 >= MAX_LIST) {
                printf("[경고] %s 의 %s_%s 칸이 너무 길어 항목 하나를 건너뜁니다.\n",
                       drugs[n-1].id, SEC_KEY[cur_sec], LANG_KEY[cur_lang]);
                continue;
            }
            if (used > 0) dst[used++] = '\x1f';
            strcpy(dst + used, item);
        }
    }
    fclose(fp);
    return n;
}

/* 필수 값이 빠진 약이 있는지 점검한다 (경고만 출력) */
static void check_drug(const Drug *d)
{
    if (d->name_ko[0] == '\0' || d->name_en[0] == '\0')
        printf("[경고] %s : 약 이름(name_ko / name_en)이 비어 있습니다.\n", d->id);
    if (d->ingredient[0] != '\0' && d->ingredient_en[0] == '\0')
        printf("[경고] %s : #ingredient_en(영문 성분명)이 없어 한국어 외 언어에서 성분 줄이 비어 보입니다.\n", d->id);
    if (d->video_url[0] == '\0')
        printf("[경고] %s : #video 주소가 없습니다.\n", d->id);
    for (int s = 0; s < N_SEC; s++) {
        int has_ko = d->list[L_KO][s][0] != '\0';
        for (int l = 1; l < N_LANG; l++) {
            int has = d->list[l][s][0] != '\0';
            if (has_ko != has)
                printf("[경고] %s : %s 칸이 한국어와 %s 중 한쪽에만 있습니다.\n", d->id, SEC_KEY[s], LANG_KEY[l]);
        }
    }
}

/* JS 문자열 안에 안전하게 넣기 위해 \ " 와 '<' 를 이스케이프하여 출력 */
static void put_js_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s; s++) {
        if (*s == '\\')      fputs("\\\\", out);
        else if (*s == '"')  fputs("\\\"", out);
        else if (*s == '<')  fputs("\\u003c", out);
        else                 fputc(*s, out);
    }
    fputc('"', out);
}

/* '\x1f' 로 이어진 항목들을 JS 배열 ["a","b"] 로 출력 */
static void put_js_array(FILE *out, const char *joined)
{
    fputc('[', out);
    if (joined[0] != '\0') {
        char buf[MAX_LIST];
        snprintf(buf, sizeof(buf), "%s", joined);
        int first = 1;
        for (char *p = buf; p != NULL; ) {
            char *next = strchr(p, '\x1f');
            if (next) *next = '\0';
            if (!first) fputc(',', out);
            put_js_string(out, p);
            first = 0;
            p = next ? next + 1 : NULL;
        }
    }
    fputc(']', out);
}

/* HTML 본문(목록 카드 등)에 글자를 넣을 때 쓰는 이스케이프 */
static void put_html(FILE *out, const char *s)
{
    for (; *s; s++) {
        if (*s == '<') fputs("&lt;", out);
        else if (*s == '>') fputs("&gt;", out);
        else if (*s == '&') fputs("&amp;", out);
        else if (*s == '"') fputs("&quot;", out);
        else fputc(*s, out);
    }
}

/* 모든 페이지가 함께 쓰는 CSS. 실제 홈페이지 느낌의 상단 로고바를 포함한다. */
void write_css(FILE *out)
{
    fprintf(out,
"<style>\n"
":root{--paper:#FBFAF7;--ink:#1C2B33;--green:#1E7A5A;--green-soft:#E4F1EB;\n"
"      --amber:#B96A00;--amber-soft:#FBF0DF;--line:#E3E0D8;--muted:#6B7680;}\n"
"*{margin:0;padding:0;box-sizing:border-box;}\n"
"html{font-size:17px;}\n"
"body{font-family:'Apple SD Gothic Neo','Noto Sans KR','Noto Sans SC','Noto Sans Thai',sans-serif;\n"
"     background:var(--paper);color:var(--ink);line-height:1.65;}\n"
"a{color:inherit;}\n"
"/* ── 사이트 상단 바: 로고 + 메뉴 ── */\n"
".topbar{position:sticky;top:0;z-index:20;background:#fff;\n"
"     border-bottom:1px solid var(--line);}\n"
".topbar-inner{max-width:720px;margin:0 auto;padding:14px 20px;\n"
"     display:flex;align-items:center;justify-content:space-between;}\n"
".logo{display:flex;align-items:center;gap:8px;text-decoration:none;}\n"
".logo-mark{width:34px;height:34px;border-radius:9px;background:var(--green);\n"
"     color:#fff;font-weight:800;font-size:1rem;display:flex;\n"
"     align-items:center;justify-content:center;flex:0 0 auto;}\n"
".logo-text{font-weight:800;font-size:1.15rem;color:var(--ink);}\n"
".logo-sub{font-size:.68rem;color:var(--muted);display:block;margin-top:-2px;}\n"
".topmenu{display:flex;gap:18px;font-size:.88rem;font-weight:600;color:var(--muted);}\n"
".topmenu a:hover{color:var(--green);}\n"
"/* ── 공통 레이아웃 ── */\n"
".wrap{max-width:720px;margin:0 auto;padding:20px 20px 80px;}\n"
"h1{font-size:1.6rem;line-height:1.3;font-weight:800;}\n"
".sub{color:var(--muted);font-size:.9rem;margin-top:4px;}\n"
".card{background:#fff;border:1px solid var(--line);border-radius:14px;\n"
"      padding:18px;margin-top:16px;}\n"
"/* ── 화면 전환 탭 (영상/글) ── */\n"
".viewtab{display:flex;gap:8px;margin-top:20px;}\n"
".viewtab button{flex:1;padding:14px 8px;border-radius:12px;font-size:1rem;\n"
"      font-weight:700;border:1.5px solid var(--line);background:#fff;\n"
"      color:var(--ink);cursor:pointer;}\n"
".viewtab button.on{background:var(--green);border-color:var(--green);color:#fff;}\n"
"/* ── 언어 탭 (글 화면 안) ── */\n"
".langtab{display:flex;flex-wrap:wrap;gap:8px;margin-top:20px;}\n"
".langtab button{padding:8px 16px;border-radius:999px;font-size:.9rem;\n"
"      font-weight:600;border:1.5px solid var(--line);background:#fff;cursor:pointer;}\n"
".langtab button.on{background:var(--ink);border-color:var(--ink);color:#fff;}\n"
".label{font-size:.8rem;font-weight:800;letter-spacing:.06em;color:var(--green);}\n"
".caution-card{background:var(--amber-soft);border-color:#EED9B8;}\n"
".caution-card .label{color:var(--amber);}\n"
".caution-card li{margin-bottom:10px;}\n"
".caution-card li:last-child{margin-bottom:0;}\n"
"iframe{width:100%%;aspect-ratio:16/9;border:0;border-radius:10px;background:#000;}\n"
".hidden{display:none;}\n"
"/* ── 글 화면: 복용 금지 대상 (3칸) ── */\n"
".sec-title{font-size:1.05rem;font-weight:800;margin-top:22px;}\n"
".card ul{margin-top:8px;padding-left:20px;}\n"
".card li{margin-bottom:8px;}\n"
".card li:last-child{margin-bottom:0;}\n"
".sub-block{margin-top:14px;padding-top:12px;border-top:1px dashed var(--line);}\n"
".sub-block:first-of-type{margin-top:6px;padding-top:0;border-top:0;}\n"
".sub-label{font-size:.92rem;font-weight:800;}\n"
".ban-card{background:#FDECEC;border-color:#F1C9C9;}\n"
".ban-card .sub-block{border-top-color:#F1C9C9;}\n"
".ban-card .lv1{color:#B3261E;}\n"
".ban-card .lv2{color:#9A5B00;}\n"
".ban-card .lv3{color:#B3261E;}\n"
/* ── 홈페이지 히어로 영역 ── */
".hero{background:var(--green-soft);border-radius:18px;padding:28px 22px;margin-top:4px;}\n"
".hero .tag{display:inline-block;background:#fff;color:var(--green);font-weight:700;\n"
"     font-size:.78rem;border-radius:999px;padding:4px 12px;}\n"
".hero h1{margin-top:12px;color:var(--green);}\n"
".hero p{margin-top:8px;color:#2E4A3F;font-size:.92rem;max-width:46ch;}\n"
".pillars{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:22px;}\n"
".pillar{background:#fff;border:1px solid var(--line);border-radius:12px;\n"
"     padding:12px 10px;text-align:center;font-size:.78rem;font-weight:700;color:var(--muted);}\n"
".pillar b{display:block;font-size:1.3rem;margin-bottom:4px;}\n"
"/* ── 약 목록 ── */\n"
".section-title{font-size:1.05rem;font-weight:800;margin-top:34px;margin-bottom:4px;}\n"
"a.drug{display:block;background:#fff;border:1px solid var(--line);\n"
"      border-radius:14px;padding:18px;margin-top:12px;text-decoration:none;\n"
"      color:var(--ink);}\n"
"a.drug:hover{border-color:var(--green);}\n"
"a.drug b{font-size:1.1rem;}\n"
"a.drug span{display:block;color:var(--muted);font-size:.85rem;margin-top:2px;}\n"
"a.back{color:var(--green);font-weight:700;text-decoration:none;font-size:.9rem;}\n"
"/* ── 하단 푸터 ── */\n"
"footer.site{max-width:720px;margin:40px auto 0;padding:20px 20px 40px;\n"
"     color:var(--muted);font-size:.78rem;border-top:1px solid var(--line);}\n"
"</style>\n");
}

/* 사이트 공통 상단 로고바 */
void write_topbar(FILE *out)
{
    fprintf(out,
"<div class=\"topbar\"><div class=\"topbar-inner\">\n"
"  <a class=\"logo\" href=\"index.html\">\n"
"    <span class=\"logo-mark\">CC</span>\n"
"    <span><span class=\"logo-text\">Co-Co</span>\n"
"    <span class=\"logo-sub\">안전 복약 가이드</span></span>\n"
"  </a>\n"
"  <nav class=\"topmenu\">\n"
"    <a href=\"index.html\">홈</a>\n"
"    <a href=\"index.html#about\">소개</a>\n"
"  </nav>\n"
"</div></div>\n");
}

/* 1) 메인 홈페이지 생성 (로고바 + 히어로 소개 + 약 목록) */
void generate_index(const Drug list[], int n)
{
    FILE *out = fopen("index.html", "w");
    if (out == NULL) { printf("[오류] index.html 생성 실패\n"); return; }

    fprintf(out,
"<!DOCTYPE html>\n<html lang=\"ko\">\n<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>Co-Co | 안전 복약 가이드</title>\n"
"<meta name=\"description\" content=\"외국인과 시청각장애인을 위한 다국어·수어 복약 안내 서비스, Co-Co\">\n");
    write_css(out);
    fprintf(out, "</head>\n<body>\n");
    write_topbar(out);

    fprintf(out,
"<div class=\"wrap\">\n"
"<div class=\"hero\" id=\"about\">\n"
"  <span class=\"tag\">누구나 안전하게 복용하는 약</span>\n"
"  <h1>Co-Co 안전 복약 가이드</h1>\n"
"  <p>언어와 장애가 복약 정보를 가로막지 않도록, 약 포장의 QR이나 NFC를 찍으면\n"
"     다국어 설명과 수어·음성 영상으로 복용법과 주의사항을 바로 확인할 수 있어요.</p>\n"
"  <div class=\"pillars\">\n"
"    <div class=\"pillar\"><b>🌐</b>언어변환 글<br>QR · 외국인용</div>\n"
"    <div class=\"pillar\"><b>🤟</b>수어·음성 영상<br>청각·시각장애인용</div>\n"
"    <div class=\"pillar\"><b>🔸</b>점자 NFC<br>터치만 하면 연결</div>\n"
"  </div>\n"
"</div>\n"
"<div class=\"section-title\">약 목록</div>\n"
"<p class=\"sub\">약을 선택하면 상세 안내로 이동합니다.</p>\n");

    for (int i = 0; i < n; i++) {
        fprintf(out, "<a class=\"drug\" href=\"%s.html\"><b>", list[i].id);
        put_html(out, list[i].name_ko);
        fprintf(out, "</b>");
        if (list[i].ingredient[0] != '\0') {
            fprintf(out, "<span>");
            put_html(out, list[i].ingredient);
            fprintf(out, "</span>");
        }
        fprintf(out, "</a>\n");
    }

    fprintf(out, "</div>\n");
    fprintf(out,
"<footer class=\"site\">Co-Co 안전 복약 가이드 · 이 사이트는 의약품 이해를 돕기 위한\n"
"안내이며, 정확한 복용은 제품 첨부문서와 의사·약사의 지시를 따르세요.</footer>\n");
    fprintf(out, "</body>\n</html>\n");
    fclose(out);
    printf("[생성] index.html (약 %d개 목록, 홈페이지 레이아웃)\n", n);
}

/* 2) 약품별 상세 페이지 생성 (영상/글 화면 전환) */
void generate_page(const Drug *d)
{
    char filename[128];
    snprintf(filename, sizeof(filename), "%s.html", d->id);

    FILE *out = fopen(filename, "w");
    if (out == NULL) { printf("[오류] %s 생성 실패\n", filename); return; }

    fputs("<!DOCTYPE html>\n<html lang=\"ko\">\n<head>\n"
          "<meta charset=\"UTF-8\">\n"
          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
          "<title>", out);
    put_html(out, d->name_ko);
    fputs(" | Co-Co 안전 복약 가이드</title>\n", out);
    write_css(out);
    fputs("</head>\n<body>\n", out);
    write_topbar(out);

    fputs("<div class=\"wrap\">\n"
          "<a class=\"back\" href=\"index.html\">&larr; 약 목록으로</a>\n"
          "<h1 style=\"margin-top:12px\" id=\"drug-name\">", out);
    put_html(out, d->name_ko);
    fputs("</h1>\n<p class=\"sub\" id=\"drug-sub\">", out);
    put_html(out, d->ingredient);
    fputs("</p>\n", out);

    /* ── 화면 전환 버튼 + 화면 A(영상) ── */
    fputs(
"<div class=\"viewtab\">\n"
"  <button id=\"btn-video\" onclick=\"setView('video')\">수어·음성 영상</button>\n"
"  <button id=\"btn-text\"  onclick=\"setView('text')\">글로 보기</button>\n"
"</div>\n"
"<div id=\"view-video\">\n"
"  <div class=\"card\">\n"
"    <div class=\"label\">수어 &amp; 음성 안내 영상</div>\n"
"    <iframe src=\"", out);
    put_html(out, d->video_url);
    fputs("\" title=\"수어 안내 영상\" allowfullscreen\n"
"            style=\"margin-top:10px\"></iframe>\n"
"    <p class=\"sub\" style=\"margin-top:10px\">영상에는 수어 통역과 음성 설명이 함께 나옵니다.</p>\n"
"  </div>\n"
"</div>\n", out);

    /* ── 화면 B: 글 (1.복용법 / 2.복용 금지 대상 (1)(2)(3) / 3.주의 사항) ── */
    fputs(
"<div id=\"view-text\" class=\"hidden\">\n"
"  <div class=\"langtab\">\n"
"    <button id=\"lang-ko\" class=\"on\" onclick=\"setLang('ko')\">한국어</button>\n"
"    <button id=\"lang-en\" onclick=\"setLang('en')\">English</button>\n"
"    <button id=\"lang-zh\" onclick=\"setLang('zh')\">中文</button>\n"
"    <button id=\"lang-vi\" onclick=\"setLang('vi')\">Tiếng Việt</button>\n"
"    <button id=\"lang-th\" onclick=\"setLang('th')\">ไทย</button>\n"
"  </div>\n"
"\n"
"  <div id=\"blk-dose\">\n"
"    <div class=\"sec-title\" id=\"h-dose\"></div>\n"
"    <div class=\"card\"><ul id=\"l-dose\"></ul></div>\n"
"  </div>\n"
"\n"
"  <div id=\"blk-ban\">\n"
"    <div class=\"sec-title\" id=\"h-ban\"></div>\n"
"    <div class=\"card ban-card\">\n"
"      <div class=\"sub-block\" id=\"blk-ban1\"><div class=\"sub-label lv1\" id=\"h-ban1\"></div><ul id=\"l-ban\"></ul></div>\n"
"      <div class=\"sub-block\" id=\"blk-consult\"><div class=\"sub-label lv2\" id=\"h-consult\"></div><ul id=\"l-consult\"></ul></div>\n"
"      <div class=\"sub-block\" id=\"blk-stop\"><div class=\"sub-label lv3\" id=\"h-stop\"></div><ul id=\"l-stop\"></ul></div>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <div id=\"blk-caution\">\n"
"    <div class=\"sec-title\" id=\"h-caution\"></div>\n"
"    <div class=\"card caution-card\"><ul id=\"l-caution\"></ul></div>\n"
"  </div>\n"
"</div>\n"
"</div>\n", out);   /* view-text 끝 + .wrap 끝 */

    fputs(
"<footer class=\"site\">Co-Co 안전 복약 가이드 · 이 페이지는 의약품 이해를 돕기 위한\n"
"안내이며, 정확한 복용은 제품 첨부문서와 의사·약사의 지시를 따르세요.</footer>\n"
"<script>\n"
"const TEXT = {\n", out);

    for (int l = 0; l < N_LANG; l++) {
        fprintf(out, "  %s: {name:", LANG_KEY[l]);
        put_js_string(out, l == L_KO ? d->name_ko : d->name_en);   /* 한국어 외에는 영어 이름 */
        fputs(", ing:", out);
        if (l == L_KO) {
            put_js_string(out, d->ingredient);
        } else if (d->ingredient_en[0] != '\0') {
            char tmp[1100];
            snprintf(tmp, sizeof(tmp), "%s%s", ING_LABEL[l], d->ingredient_en);
            put_js_string(out, tmp);
        } else {
            put_js_string(out, "");
        }
        for (int s = 0; s < N_SEC; s++) {
            fprintf(out, ", %s:", SEC_KEY[s]);
            put_js_array(out, d->list[l][s]);
        }
        fputs(l < N_LANG - 1 ? "},\n" : "}\n", out);
    }

    fputs(
"};\n"
"const LABEL = {\n"
"  ko: {dose:'1. 복용법 (용법·용량)', ban:'2. 복용 금지 대상',\n"
"       ban1:'(1) 다음과 같은 사람은 이 약을 복용하지 말 것',\n"
"       consult:'(2) 복용 전 의사·약사와 상의해야 하는 대상',\n"
"       stop:'(3) 즉각 복용 중지하고 의사, 약사와 상의해야 하는 대상',\n"
"       caution:'3. 주의 사항'},\n"
"  en: {dose:'1. How to take (dose)', ban:'2. Who should not take this medicine',\n"
"       ban1:'(1) Do NOT take this medicine if you are in any of these groups',\n"
"       consult:'(2) Talk to a doctor or pharmacist BEFORE taking if',\n"
"       stop:'(3) STOP taking it right away and talk to a doctor or pharmacist if',\n"
"       caution:'3. Precautions'},\n"
"  zh: {dose:'1. 用法用量', ban:'2. 禁止服用的人群',\n"
"       ban1:'(1) 属于以下情况的人，请勿服用本药',\n"
"       consult:'(2) 服药前需先咨询医生或药师的人群',\n"
"       stop:'(3) 出现以下情况，请立即停药并咨询医生或药师',\n"
"       caution:'3. 注意事项'},\n"
"  vi: {dose:'1. Cách dùng (liều lượng)', ban:'2. Đối tượng không được dùng',\n"
"       ban1:'(1) KHÔNG dùng thuốc này nếu bạn thuộc các trường hợp sau',\n"
"       consult:'(2) Hỏi bác sĩ hoặc dược sĩ TRƯỚC khi dùng nếu',\n"
"       stop:'(3) NGỪNG dùng thuốc ngay và hỏi bác sĩ hoặc dược sĩ nếu',\n"
"       caution:'3. Lưu ý'},\n"
"  th: {dose:'1. วิธีใช้ (ขนาดยา)', ban:'2. ผู้ที่ไม่ควรใช้ยา',\n"
"       ban1:'(1) ห้ามใช้ยานี้หากอยู่ในกลุ่มต่อไปนี้',\n"
"       consult:'(2) ปรึกษาแพทย์หรือเภสัชกรก่อนใช้ยา หาก',\n"
"       stop:'(3) หยุดใช้ยาทันทีและปรึกษาแพทย์หรือเภสัชกร หาก',\n"
"       caution:'3. ข้อควรระวัง'}\n"
"};\n"
"function fill(id, items){\n"
"  const ul = document.getElementById('l-' + id);\n"
"  ul.innerHTML = '';\n"
"  items.forEach(t => { const li = document.createElement('li'); li.textContent = t; ul.appendChild(li); });\n"
"}\n"
"function show(blockId, on){ document.getElementById(blockId).classList.toggle('hidden', !on); }\n"
"function setView(v){\n"
"  document.getElementById('view-video').classList.toggle('hidden', v!=='video');\n"
"  document.getElementById('view-text').classList.toggle('hidden', v!=='text');\n"
"  document.getElementById('btn-video').classList.toggle('on', v==='video');\n"
"  document.getElementById('btn-text').classList.toggle('on', v==='text');\n"
"}\n"
"function setLang(l){\n"
"  const t = TEXT[l], lab = LABEL[l];\n"
"  document.documentElement.lang = l;\n"
"  document.getElementById('drug-name').textContent = t.name;\n"
"  document.getElementById('drug-sub').textContent = t.ing;\n"
"  ['dose','ban','caution'].forEach(k => document.getElementById('h-' + k).textContent = lab[k]);\n"
"  ['ban1','consult','stop'].forEach(k => document.getElementById('h-' + k).textContent = lab[k]);\n"
"  fill('dose', t.dose); fill('ban', t.ban); fill('consult', t.consult);\n"
"  fill('stop', t.stop); fill('caution', t.caution);\n"
"  show('blk-dose', t.dose.length > 0);\n"
"  show('blk-ban1', t.ban.length > 0);\n"
"  show('blk-consult', t.consult.length > 0);\n"
"  show('blk-stop', t.stop.length > 0);\n"
"  show('blk-ban', t.ban.length + t.consult.length + t.stop.length > 0);\n"
"  show('blk-caution', t.caution.length > 0);\n"
"  ['ko','en','zh','vi','th'].forEach(x => document.getElementById('lang-' + x).classList.toggle('on', x===l));\n"
"}\n"
"const first = new URLSearchParams(location.search).get('view') || 'video';\n"
"setView(first === 'text' ? 'text' : 'video');\n"
"setLang('ko');\n"
"</script>\n"
"</body>\n</html>\n", out);

    fclose(out);
    printf("[생성] %s\n", filename);
    /* 접근성 이유로 입구를 재배치함:
       - NFC(+ 옆에 점자 표시)는 조준 없이 "터치"만 하면 되므로 시각장애인에게 적합
         -> 음성설명이 포함된 영상 화면을 기본으로 띄운다.
       - QR은 카메라로 "봐야" 찍히므로 외국인·청각장애인 등 보이는 사용자에게 적합
         -> 번역 가능한 글 화면을 기본으로 띄운다. */
    printf("       QR용 주소 : https://ryesea.github.io/Safe-Medication-Guide/%s?view=text\n", filename);
    printf("       NFC용 주소: https://ryesea.github.io/Safe-Medication-Guide/%s?view=video\n", filename);
}

int main(void)
{
    int n = load_drugs("drugs.txt");
    if (n < 0) {
        printf("[오류] drugs.txt 파일을 찾을 수 없습니다.\n");
        return 1;
    }

    printf("총 %d개 약품 데이터를 읽었습니다.\n\n", n);
    for (int i = 0; i < n; i++) check_drug(&drugs[i]);

    generate_index(drugs, n);
    for (int i = 0; i < n; i++)
        generate_page(&drugs[i]);

    printf("\nCo-Co 웹사이트 생성 완료!\n");
    return 0;
}
