# Лабораторная № 3 · разбор нового кода
Версия с двумя конусами и материалами Mona Lisa, Spiral, Shark и Doge. Разбор построен по сравнению исходников lab2 и lab3. Фрагменты приведены из текущей lab3; строки вокруг изменений оставлены там, где они нужны для понимания.

**Как читать.** Сначала проследи один кадр на бумаге. В первом списке выбран Doge, во втором — Shark. Оба объекта используют одни вершины и индексы. Первый draw получает набор Doge и смещение 0, второй — набор Shark и смещение model_stride. Изображение выбирается набором, положение — записью модели, эффект — push-константами. Теперь разберём, как программа получила каждый из этих ресурсов.

## 1. Геометрия · что хранит вершина

**source/cone_mesh.hpp, строки 9–15**

```cpp
struct ConeVertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 uv{};
};
struct ConeMesh {
```

**source/cone_mesh.hpp, строки 22–44**

```cpp
inline ConeMesh makeConeMesh() {
    ConeMesh mesh;
    const glm::vec3 top(0, cone_height / 2, 0), center(0, -cone_height / 2, 0), down(0, -1, 0);
    for (uint32_t i = 0; i < cone_segments; ++i) {
        const float a = 2 * std::numbers::pi_v<float> * float(i) / float(cone_segments);
        const float b = 2 * std::numbers::pi_v<float> * float(i + 1) / float(cone_segments);
        const glm::vec3 p(cone_radius * std::cos(a), center.y, cone_radius * std::sin(a));
        const glm::vec3 q(cone_radius * std::cos(b), center.y, cone_radius * std::sin(b));
        const glm::vec3 np = glm::normalize(glm::vec3(std::cos(a), cone_radius / cone_height, std::sin(a)));
        const glm::vec3 nq = glm::normalize(glm::vec3(std::cos(b), cone_radius / cone_height, std::sin(b)));
        const glm::vec3 nt = glm::normalize(np + nq);
        const uint32_t first = uint32_t(mesh.vertices.size());
        const float u = float(i) / cone_segments, v = float(i + 1) / cone_segments;
        auto diskUV = [](glm::vec3 pos) { return glm::vec2(0.5f) + glm::vec2(pos.x,pos.z) / (2 * cone_radius); };
        mesh.vertices.insert(mesh.vertices.end(), {
            {top, {}, nt, {(u+v)/2, 0}}, {q, {}, nq, {v, 1}}, {p, {}, np, {u, 1}},
            {center, {}, down, diskUV(center)}, {p, {}, down, diskUV(p)}, {q, {}, down, diskUV(q)}
        });
        for (uint32_t j = 0; j < 6; ++j) mesh.indices.push_back(first + j);
    }
    return mesh;
}
} // namespace application
```

В ConeVertex добавлена пара чисел uv. Поле color осталось нужным для цветных маркеров света. У конуса цвет вершины теперь заполняется пустым и не участвует в выборе цвета; прежняя функция vertexColor удалена.

Для сектора i точки p и q лежат на окружности при соседних углах a и b. np и nq — наклонные нормали боковой поверхности; вертикальная компонента зависит от отношения радиуса к высоте. nt — усреднённая нормаль вершины этого сектора. У основания нормаль down. Один и тот же край должен иметь разные нормали и UV со стороны основания и со стороны бока, поэтому в массив попадают отдельные вершины.

**Тонкость с именем v.** Локальные u и v в этой функции — два соседних значения *горизонтальной* координаты U: i/64 и (i+1)/64. Вертикальная координата V задаётся прямо числами 0 у вершины и 1 у обода. Для сектора 0 горизонтальный интервал равен [0, 1/64], для последнего — [63/64, 1]. Последний треугольник не интерполирует U от почти 1 назад к 0 через всю картинку. Первые три записи образуют боковой треугольник. Последние три — основание. diskUV переводит (x,z) в (0.5+x/(2r), 0.5+z/(2r)); центр основания получает (0.5,0.5). first+j записывает индексы шести только что добавленных вершин. Получается 64 × 6 = 384 индекса.

## 2. Состояние интерфейса и данные кадра

**source/scene.hpp, строки 12–18**

```cpp
struct Material {
    glm::vec3 albedo{0.65f, 0.7f, 0.8f};
    glm::vec3 specular{0.8f};
    float shininess = 48.0f;
    int textures[2]{1, 0};
    bool effect = true;
};
```

**source/application.cpp, строки 24–33**

```cpp
glm::mat4 makeModel(int cone) {
    const AnimationPose pose = animationPose(controls.animation);
    const glm::vec3 offset(pose.position[0], pose.position[1], pose.position[2]);
    const glm::vec3 angles = controls.rotation + glm::vec3(pose.rotation[0], pose.rotation[1], pose.rotation[2]);
    glm::mat4 model = glm::translate(glm::mat4(1), controls.position + offset + glm::vec3(cone == 0 ? -1.1f : 1.1f, 0, 0));
    model = glm::rotate(model, glm::radians(angles.x), glm::vec3(1,0,0));
    model = glm::rotate(model, glm::radians(angles.y), glm::vec3(0,1,0));
    model = glm::rotate(model, glm::radians(angles.z), glm::vec3(0,0,1));
    return glm::scale(model, controls.scale);
}
```

**source/application.cpp, строки 65–74**

```cpp
    ImGui::SliderFloat3("Scale", &controls.scale.x, 0.1f, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    Material& material = controls.material;
    ImGui::ColorEdit3("Albedo", &material.albedo.x);
    ImGui::ColorEdit3("Specular color", &material.specular.x);
    ImGui::SliderFloat("Shininess", &material.shininess, 1, 256, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    const char* materials[] = {"Mona Lisa", "Spiral", "Shark", "Doge"};
    ImGui::Combo("First cone", &material.textures[0], materials, IM_ARRAYSIZE(materials));
    ImGui::Combo("Second cone", &material.textures[1], materials, IM_ARRAYSIZE(materials));
    ImGui::Checkbox("Animate first texture", &material.effect);
    if (ImGui::TreeNode("Animation")) {
```

textures[2] хранит **номера материалов**, а не пиксели и не Vulkan-объекты. Начальные значения {1,0} дают Spiral слева и Mona Lisa справа. effect по умолчанию включён. ImGui::Combo получает адрес конкретного целого числа и меняет его, когда пользователь выбирает строку. IM_ARRAYSIZE передаёт количество вариантов, то есть 4. Checkbox так же меняет bool. Передача адреса нужна, чтобы виджет записал результат в состояние программы.

makeModel(int cone) берёт прежнюю позицию, вращение, масштаб и позу анимации. Добавляется смещение по X: −1.1 для первого конуса, +1.1 для второго. Оба конуса пользуются общими настройками трансформации, но занимают разные места. Отдельно выбирается их текстура. Галочка Animate first texture управляет выборкой пикселей; раздел Animation управляет движением геометрии.

В spotLightsUi кнопки Aim at first cone и Aim at second cone используют makeModel(0)[3] или makeModel(1)[3]. Четвёртый столбец матрицы GLM содержит перенос. Разность «позиция выбранного конуса минус позиция прожектора» даёт направление света в момент нажатия. В update и в main.cpp заменены заголовки панели и окна на Lab 3.

**source/application.cpp, строки 206–212**

```cpp
    graphics::ModelUniform models[2];
    for (int i = 0; i < 2; ++i) {
        models[i].model = makeModel(i);
        models[i].normal_matrix = glm::transpose(glm::inverse(models[i].model));
        models[i].albedo = glm::vec4(controls.material.albedo, 1.0f);
        models[i].specular = glm::vec4(controls.material.specular, controls.material.shininess);
    }
```

**source/application.cpp, строки 225–226**

```cpp
    return graphics::renderScene(frame, scene, models, controls.material.textures,
                                 controls.material.effect, float(ImGui::GetTime()), points, spots, makeLightMarkers(lighting));
```

**source/graphics.hpp, строки 8–12**

```cpp
bool initializeCone(const application::ConeMesh& mesh);
bool renderScene(const internal::FrameData& frame,
                 const SceneUniform& scene, const ModelUniform (&models)[2], const int (&materials)[2], bool animated, float time,
                 const std::vector<PointLightGpu>& points, const std::vector<SpotLightGpu>& spots,
                 const application::LightMarkers& markers);
```

render создаёт две структуры на CPU. model преобразует координаты вершины в мировые; transpose(inverse(model)) нужна для правильного преобразования нормали, особенно при различном масштабе вдоль осей. albedo.rgb служит множителем фотографии, w заполняется единицей. У specular компоненты RGB задают оттенок блика, w — показатель степени. В gpu_data.hpp изменился комментарий к albedo; двоичный размер ModelUniform сохранился: две матрицы по 64 байта и два vec4 по 16 = 160 байт.

Вместо одной модели renderScene получает обе структуры, оба номера материалов, флаг и время ImGui в секундах. Запись const ModelUniform (&models)[2] означает «ссылка на массив ровно из двух структур, доступных для чтения». У массива индексов тот же принцип. Векторы источников света и маркеры передаются, как в lab2.

## 3. Ресурсы Vulkan · что появляется при запуске

**source/graphics.cpp, строки 1–7**

```cpp
#include "graphics.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#include <memory>
#include <climits>

```

**source/graphics.cpp, строки 39–55**

```cpp
Buffer vertex_buffer, index_buffer, scene_buffer, model_buffer;
Buffer point_buffer, spot_buffer, marker_buffer, line_buffer;
Buffer marker_staging, line_staging;
uint32_t index_count = 0;
VkDeviceSize max_storage_range = 0;
VkDescriptorSetLayout descriptor_layouts[2]{};
VkDescriptorSet descriptor_sets[5]{}; // scene and four materials
VkDeviceSize model_stride;
struct Texture {
    VkImage image{};
    VmaAllocation allocation{};
    VkImageView view{};
};
Texture textures[7];
VkSampler sampler{};
VkCommandPool upload_pool{};
struct Effect { int32_t enabled; float time; };
```

STB_IMAGE_IMPLEMENTATION помещает реализацию библиотеки в этот файл компиляции. STBI_ONLY_PNG оставляет декодер PNG. memory нужен для unique_ptr, climits — для INT_MAX. stb_image.h — сторонний декодер; собственный код вызывает его в loadTexture.

Texture хранит три разных вещи. VkImage — объект изображения, VmaAllocation — информация о выделенной под него памяти, VkImageView — представление, которое привязывается для чтения шейдером. textures[7] — семь физических изображений для четырёх материалов, включая две карты эффектов и общую чёрную карту. sampler один для всех: правила фильтрации одинаковы. upload_pool выделяет командные буферы разовых загрузок. descriptor_sets[5] содержит один набор сцены и четыре материала. descriptor_layouts[2] содержит только две схемы: схема сцены и общая схема материала. model_stride измеряется в байтах. Effect состоит из int32_t и float — всего 8 байт. Целый флаг совпадает с GLSL int по размеру.

**source/graphics.cpp, строки 477–502**

```cpp
bool initializeCone(const application::ConeMesh& mesh) {
    try {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(internal::context.physical_device, &properties);
        max_storage_range = properties.limits.maxStorageBufferRange;
        index_count = uint32_t(mesh.indices.size());
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = internal::context.graphics_queue_index;
        const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
        model_stride = (sizeof(ModelUniform) + alignment - 1) / alignment * alignment;
        if (check(vkCreateCommandPool(internal::context.device,&pool,nullptr,&upload_pool), "Create upload pool") &&
            createTextures() && uploadMesh(mesh.vertices.size() * sizeof(ConeVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertices.data(), vertex_buffer) &&
            uploadMesh(mesh.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indices.data(), index_buffer) &&
            createBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, scene_buffer) &&
            createBuffer(model_stride * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr, model_buffer) &&
            createBuffer(sizeof(PointLightGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr, point_buffer) &&
            createBuffer(sizeof(SpotLightGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr, spot_buffer) &&
            createDescriptors() &&
            createPipeline("cone", false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pipeline) &&
            createPipeline("marker", true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, marker_pipeline) &&
            createPipeline("marker", true, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, line_pipeline)) return true;
    } catch (const std::exception& error) {
        std::cerr << "Resource initialization failed: " << error.what() << '\n';
    }
    shutdownCone();
    return false;
```

initializeCone — точка, где эти части соединяются. Из свойств физического устройства берётся требование выравнивания A. Выражение (160+A−1)/A*A использует целочисленное деление и округляет вверх до кратного A. Например, при A=256 получаем шаг 256 байт, размер буфера 512, адреса записей 0 и 256. Число 256 — пример, не зафиксированная настройка.

Пул загрузки создаётся для того семейства очередей, куда пойдут команды. Затем создаются текстуры, загружаются вершины и индексы, выделяются буферы сцены, моделей и света. Только теперь можно записать их в дескрипторы и создать конвейеры. index_count берётся из mesh.indices.size(). Цепочка && прекращает инициализацию на первой ошибке. Частично созданные ресурсы затем передаются на очистку.

## 4. Передача данных с CPU на GPU

**source/graphics.cpp, строки 146–157**

```cpp
bool deviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer.capacity = size;
    return check(vmaCreateBuffer(internal::context.allocator, &info, &alloc, &buffer.handle,
                                 &buffer.allocation, nullptr), "Allocate GPU buffer");
}

```

deviceBuffer создаёт конечный буфер. size задаётся в байтах. usage сообщает, как буфер будет использован после загрузки (например, как индексный), а TRANSFER_DST разрешает записывать в него командой копирования. AUTO_PREFER_DEVICE выражает предпочтение, requiredFlags DEVICE_LOCAL делает память устройства обязательным требованием. capacity нужна, чтобы позже понять, хватает ли места. В отличие от ранее существовавшего createBuffer для отображаемой памяти, этот путь не запрашивает CPU-указатель.

**source/graphics.cpp, строки 158–183**

```cpp
template<class Record>
bool upload(const void* data, VkDeviceSize size, Record record) {
    const auto& ctx = internal::context;
    Buffer staging;
    VkCommandBuffer cmd{};
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = upload_pool;
    alloc.commandBufferCount = 1;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool ok = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data, staging)
        && check(vkAllocateCommandBuffers(ctx.device, &alloc, &cmd), "Allocate upload command")
        && check(vkBeginCommandBuffer(cmd, &begin), "Begin upload");
    if (ok) {
        record(cmd, staging.handle);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        ok = check(vkEndCommandBuffer(cmd), "End upload")
            && check(vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE), "Submit upload")
            && check(vkQueueWaitIdle(ctx.graphics_queue), "Wait for upload");
    }
    if (cmd) vkFreeCommandBuffers(ctx.device, upload_pool, 1, &cmd);
    destroyBuffer(staging);
    return ok;
}
```

upload обслуживает разовую загрузку и для сетки, и для изображений. data — указатель на исходные байты, size — их количество. Record — переданная функция, которая записывает конкретную команду копирования. Поэтому общий код выделения и ожидания используется в двух случаях.

1. createBuffer с TRANSFER_SRC создаёт staging-буфер в доступной CPU памяти, копирует data через memcpy и вызывает vmaFlushAllocation. Flush делает запись видимой устройству на некогерентной памяти.
2. vkAllocateCommandBuffers выделяет один командный буфер из upload_pool. vkBeginCommandBuffer с ONE_TIME_SUBMIT открывает его для однократной отправки.
3. record(cmd, staging.handle) **записывает** инструкции. На этом шаге GPU ещё не обязан ничего скопировать.
4. vkEndCommandBuffer завершает запись, vkQueueSubmit отправляет буфер графической очереди, vkQueueWaitIdle дожидается выполнения. Это простое ожидание используется при загрузке ресурсов, а не при каждом выборе материала.
5. После ожидания освобождаются cmd и staging. Если уничтожить staging сразу после submit, GPU мог бы читать уже освобождённую память. ok и check передают вызывающей стороне результат этапов.

**source/graphics.cpp, строки 185–196**

```cpp
bool uploadMesh(VkDeviceSize size, VkBufferUsageFlags usage, const void* data, Buffer& buffer) {
    if (!deviceBuffer(size, usage, buffer)) return false;
    return upload(data, size, [&](VkCommandBuffer cmd, VkBuffer src) {
        VkBufferCopy copy{0,0,size};
        vkCmdCopyBuffer(cmd, src, buffer.handle, 1, &copy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    });
}
```

uploadMesh получает готовые байты сетки. Сначала deviceBuffer выделяет конечную память, затем лямбда записывает vkCmdCopyBuffer. VkBufferCopy{0,0,size} означает «с начала источника в начало назначения, size байт». После копирования барьер связывает стадию TRANSFER с VERTEX_INPUT. Маска источника — запись при переносе, назначения — чтение индексов или атрибутов. Один и тот же код вызывается для массива ConeVertex и для массива uint32_t. Очередь исполняет одинаковый перенос байтов; назначение буфера различается.

## 5. От файла PNG до текстуры в шейдере

**source/graphics.cpp, строки 198–212**

```cpp
bool loadTexture(const char* name, VkFormat format, Texture& texture) {
    const auto path = shaderDirectory().parent_path() / "textures" / name;
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.empty() || bytes.size() > INT_MAX) {
        std::cerr << "Invalid texture file: " << name << '\n';
        return false;
    }
    int w, h, channels;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4), stbi_image_free);
    if (!pixels) { std::cerr << "Cannot load " << name << '\n'; return false; }
    const auto& ctx = internal::context;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
```

Путь строится от каталога исполняемого файла, поэтому выбор рабочей папки при запуске не меняет расположение ресурсов на Windows. ifstream открывает файл как двоичный. bytes содержит закодированный PNG. Проверки отсекают пустой ввод и размер, не представимый типом int у декодера. stbi_load_from_memory получает сжатый массив, возвращает ширину и высоту и декодирует в четыре компоненты на пиксель независимо от исходного числа каналов. unique_ptr использует stbi_image_free как функцию освобождения: распакованные пиксели будут освобождены и при раннем выходе.

**source/graphics.cpp, строки 213–231**

```cpp
    info.format = format;
    info.extent = {uint32_t(w), uint32_t(h), 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (!check(vmaCreateImage(ctx.allocator, &info, &alloc, &texture.image, &texture.allocation, nullptr),
               "Create texture")) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = texture.image;
    view.format = format;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(ctx.device, &view, nullptr, &texture.view), "Create texture view")) return false;
    return upload(pixels.get(), VkDeviceSize(w)*h*4, [&](VkCommandBuffer cmd, VkBuffer src) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
```

imageType = 2D, extent = (ширина, высота, 1). Здесь один уровень детализации, один слой, один образец на пиксель. Формат передан вызывающей функцией. SAMPLED разрешает чтение через сэмплер, TRANSFER_DST — загрузку пикселей. VMA создаёт VkImage и выделяет device-local память. View относится к цветовой составляющей (ASPECT_COLOR), уровню 0 и слою 0. Это описание диапазона исходного изображения, а не копия его данных.

**source/graphics.cpp, строки 232–248**

```cpp
        barrier.image = texture.image;
        barrier.subresourceRange = view.subresourceRange;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = info.extent;
        vkCmdCopyBufferToImage(cmd, src, texture.image, barrier.newLayout, 1, &copy);
        barrier.oldLayout = barrier.newLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
```

upload получает w × h × 4 байт. Первый барьер переводит только что созданное изображение из UNDEFINED (нулевое начальное значение oldLayout) в TRANSFER_DST_OPTIMAL. Предыдущего содержимого сохранять не требуется. Очереди не меняются, поэтому оба индекса семейства равны IGNORED. dstAccessMask разрешает трансферную запись. vkCmdCopyBufferToImage копирует цветовой слой 0 целиком; нулевые шаги строк в VkBufferImageCopy означают плотно упакованные пиксели.

Второй барьер берёт прежнее назначение как oldLayout и ставит SHADER_READ_ONLY_OPTIMAL как newLayout. Источник — TRANSFER_WRITE на стадии TRANSFER, назначение — SHADER_READ на стадии FRAGMENT_SHADER. Таким образом код решает обе задачи: переводит изображение в подходящий режим использования и обеспечивает видимость записанных пикселей до чтения.

**source/graphics.cpp, строки 251–263**

```cpp
bool createTextures() {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.minFilter = info.magFilter = VK_FILTER_LINEAR;
    if (!check(vkCreateSampler(internal::context.device, &info, nullptr, &sampler), "Create sampler")) return false;
    const char* files[] = {"mona-lisa.png","spiral.png","shark.png","specular.png","emissive.png","black.png","doge.png"};
    for (int i = 0; i < 7; ++i) {
        if ((i == 3 || i == 4) && !std::filesystem::exists(shaderDirectory().parent_path() / "textures" / files[i]))
            continue;
        VkFormat format = i == 3 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
        if (!loadTexture(files[i], format, textures[i])) return false;
    }
    return true;
}
```

minFilter и magFilter равны LINEAR: при уменьшении и увеличении GPU смешивает соседние тексели. Нулевые значения режимов адресации в инициализированной структуре соответствуют REPEAT. Поэтому uv*2 в эффекте может выйти за [0,1] и повторить рисунок. Цепочка mip-уровней не создаётся.

Порядок files определяет индексы в таблице материалов ниже. Для specular (индекс 3) выбирается UNORM: значение является числовой маской. Для карт цвета используется SRGB: при чтении цвет преобразуется в линейные значения для расчёта освещения. При отсутствии необязательной specular или emissive цикл пропускает её. Обязательные карты при ошибке декодирования прерывают инициализацию с именем файла.

## 6. Как шейдер получает именно нужные ресурсы

**source/graphics.cpp, строки 308–324**

```cpp
void updateDescriptors() {
    const VkDescriptorBufferInfo infos[] = {
        {scene_buffer.handle, 0, sizeof(SceneUniform)},
        {point_buffer.handle, 0, point_buffer.capacity},
        {spot_buffer.handle, 0, spot_buffer.capacity}
    };
    VkWriteDescriptorSet writes[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor_sets[0];
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = (i == 1 || i == 2) ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(internal::context.device, 3, writes, 0, nullptr);
}
```

**source/graphics.cpp, строки 326–345**

```cpp
bool createDescriptors() {
    const auto device = internal::context.device;
    const VkDescriptorSetLayoutBinding scene_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    const VkDescriptorSetLayoutBinding model_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    for (int i = 0; i < 2; ++i) {
        const VkDescriptorSetLayoutCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = i == 0 ? 3u : 4u,
            .pBindings = i == 0 ? scene_bindings : model_bindings
        };
        if (!check(vkCreateDescriptorSetLayout(device, &info, nullptr, &descriptor_layouts[i]), "Create descriptor layout"))
```

updateDescriptors записывает три привязки в descriptor_sets[0]. Binding 0 — UBO сцены, binding 1 — storage buffer точечных источников, binding 2 — storage buffer прожекторов. VkDescriptorBufferInfo содержит handle, смещение и диапазон. Массив writes связывает эти описания с номерами входов. Сами пиксели или структуры вызов vkUpdateDescriptorSets не копирует.

createDescriptors объявляет второй layout. У материала binding 0 — UNIFORM_BUFFER_DYNAMIC, доступный обоим шейдерам. Binding 1, 2, 3 — COMBINED_IMAGE_SAMPLER, доступные фрагментному шейдеру: основная картинка, маска блика, свечение соответственно. Это соответствует layout(set=1,binding=...) в GLSL. Число set в шейдере означает место набора в конвейере, а не индекс материала в C++ массиве.

**source/graphics.cpp, строки 348–387**

```cpp
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}
    };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 5, .poolSizeCount = 4, .pPoolSizes = sizes
    };
    if (!check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool), "Create descriptor pool")) return false;
    VkDescriptorSetLayout layouts[]{descriptor_layouts[0], descriptor_layouts[1], descriptor_layouts[1], descriptor_layouts[1], descriptor_layouts[1]};
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool, .descriptorSetCount = 5, .pSetLayouts = layouts
    };
    if (!check(vkAllocateDescriptorSets(device, &allocate, descriptor_sets), "Allocate descriptors")) return false;
    updateDescriptors();
    const int maps[4][3] = {{0,5,5},{1,3,4},{2,5,5},{6,5,5}};
    VkDescriptorBufferInfo models{model_buffer.handle, 0, sizeof(ModelUniform)};
    for (int i = 0; i < 4; ++i) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor_sets[i+1];
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &models;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        for (int j = 0; j < 3; ++j) {
            VkImageView view = textures[maps[i][j]].view;
            VkDescriptorImageInfo image{sampler, view ? view : textures[5].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            write.dstBinding = j+1;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }
    VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Effect)};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = descriptor_layouts,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &range
    };
    return check(vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout), "Create pipeline layout");
```

Размеры пула получены подсчётом: 1 обычный UBO сцены, 2 буфера света, 4 дескриптора динамического UBO (по одному на материал) и 4 × 3 = 12 пар изображение–сэмплер. Физический model_buffer при этом один. Выделяются 5 наборов, первым схема сцены, затем четыре раза схема материала.

| Выбор | Набор C++ | maps[i] | Что реально читает шейдер |
|---|---:|---|---|
| Mona Lisa (0) | 1 | 0,5,5 | Портрет, чёрный, чёрный |
| Spiral (1) | 2 | 1,3,4 | Спираль, specular, emissive |
| Shark (2) | 3 | 2,5,5 | Акула, чёрный, чёрный |
| Doge (3) | 4 | 6,5,5 | Собака, чёрный, чёрный |

Первый write в каждый набор связывает binding 0 с model_buffer, базовым смещением 0 и диапазоном **одной структуры**, sizeof(ModelUniform). Смещение конкретной модели будет добавлено позже при draw. Внутренний цикл ставит dstBinding=j+1 и привязывает view с общим sampler. Если необязательный view пуст, используется textures[5] — чёрный пиксель. Один набор полностью описывает материал, поэтому при выборе в интерфейсе достаточно привязать готовый набор.

VkPushConstantRange объявляет диапазон [0,8) для фрагментной стадии. Pipeline layout объединяет обе схемы наборов и этот диапазон. Объявление диапазона ещё не задаёт значения флага и времени — они будут записаны перед рисованием.

## 7. Как конвейер читает вершины

**source/graphics.cpp, строки 413–423**

```cpp
    const VkVertexInputBindingDescription vertex_binding{0, sizeof(ConeVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ConeVertex, position)},
        {marker ? 1u : 3u, 0, marker ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32_SFLOAT, marker ? uint32_t(offsetof(ConeVertex, color)) : uint32_t(offsetof(ConeVertex, uv))},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ConeVertex, normal)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = marker ? 2u : 3u, .pVertexAttributeDescriptions = attributes,
    };
```

vertex_binding сообщает шаг до следующей вершины — sizeof(ConeVertex). offsetof даёт смещение поля внутри одной вершины. Для конуса GPU читает позицию (location 0, три float), UV (location 3, два float) и нормаль (location 2, три float). Для маркера берутся только первые две строки описания: позиция и **цвет** (location 1, три float). Тернарные выражения в средней строке одновременно переключают номер входа, формат и смещение. Это позволяет использовать общий тип вершины и прежний шейдер маркеров. Ошибка в одном из трёх полей могла бы заставить GPU интерпретировать цвет как UV.

## 8. Что обновляется и рисуется каждый кадр

**source/graphics.cpp, строки 293–306**

```cpp
bool prepareMarkers(Buffer& gpu, Buffer& staging, const void* vertices, VkDeviceSize bytes) {
    if (bytes > gpu.capacity) {
        destroyBuffer(gpu);
        if (!deviceBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu)) return false;
    }
    return ensureBufferSize(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        && writeBuffer(staging, vertices, bytes);
}

void copyMarkers(VkCommandBuffer cmd, const Buffer& staging, const Buffer& gpu, VkDeviceSize size) {
    if (size == 0) return;
    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.handle, gpu.handle, 1, &copy);
}
```

**source/graphics.cpp, строки 544–562**

```cpp
    // при пустом массиве буфер всё ещё содержит место для одного элемента, а счётчик равен нулю.
    const VkDeviceSize point_bytes = points.size() * sizeof(PointLightGpu);
    const VkDeviceSize spot_bytes = spots.size() * sizeof(SpotLightGpu);
    const VkDeviceSize marker_bytes = markers.triangles.size() * sizeof(ConeVertex);
    const VkDeviceSize line_bytes = markers.lines.size() * sizeof(ConeVertex);
    const bool lights_resized = point_bytes > point_buffer.capacity || spot_bytes > spot_buffer.capacity;
    if (!ensureBufferSize(point_buffer, point_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !ensureBufferSize(spot_buffer, spot_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !prepareMarkers(marker_buffer, marker_staging, markers.triangles.data(), marker_bytes) ||
        !prepareMarkers(line_buffer, line_staging, markers.lines.data(), line_bytes)) return false;
    if (lights_resized) updateDescriptors();
    for (int i = 0; i < 2; ++i)
        std::memcpy(static_cast<char*>(model_buffer.mapped) + i * model_stride, &models[i], sizeof(ModelUniform));
    if (!check(vmaFlushAllocation(context.allocator, model_buffer.allocation, 0, model_buffer.capacity), "Flush models")) return false;
    if (!writeBuffer(scene_buffer, &scene, sizeof(scene)) ||
        !writeBuffer(point_buffer, points.data(), point_bytes) || !writeBuffer(spot_buffer, spots.data(), spot_bytes)) return false;

    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
```

Маркеры показывают позиции источников света треугольниками, а направления прожекторов — линиями. Они могут двигаться и менять количество вершин, поэтому им нужны переиспользуемые staging-буферы marker_staging и line_staging. prepareMarkers расширяет конечный GPU-буфер, только если не хватает capacity; ensureBufferSize аналогично проверяет временный буфер. writeBuffer пишет вершины в доступную CPU память и выполняет flush. Пустой массив допустим: размер нулевой, чтения указателя и копирования не требуется. Обновление происходит после ожидания fence предыдущего кадра в graphics_internal.cpp: предыдущий кадр больше не читает данные, которые сейчас перезаписываются.

lights_resized вычисляется **до** увеличения буферов. Если точечные источники или прожекторы перестали помещаться, ensureBufferSize меняет буфер. Дескриптор всё ещё указывал бы на старый handle, поэтому updateDescriptors вызывается после замены. При изменении только интенсивности или позиции достаточно переписать данные в том же буфере.

Модели записываются memcpy по адресу mapped + i*model_stride. Приведение к char* делает сложение побайтовым. Копируются 160 байт каждой структуры, а не model_stride байт: промежуток служит для выравнивания. После обеих записей vmaFlushAllocation делает обновлённую область доступной устройству. Буфер сцены и массивы света обновляются отдельными вызовами writeBuffer.

**source/graphics.cpp, строки 564–576**

```cpp
    };
    if (!check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin command buffer")) return false;
    if (marker_bytes || line_bytes) {
        copyMarkers(fd.command_buffer, marker_staging, marker_buffer, marker_bytes);
        copyMarkers(fd.command_buffer, line_staging, line_buffer, line_bytes);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(fd.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }
    const VkClearValue clear[] = {
        {.color = {{0.025f, 0.035f, 0.055f, 1.0f}}}, {.depthStencil = {1.0f, 0}}
```

**source/graphics.cpp, строки 588–602**

```cpp
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                            0, 1, descriptor_sets, 0, nullptr);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);
    for (int i = 0; i < 2; ++i) {
        uint32_t dynamic_offset = uint32_t(i * model_stride);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                                1, 1, &descriptor_sets[1 + materials[i]], 1, &dynamic_offset);
        Effect effect{i == 0 && animated, time};
        vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(effect), &effect);
        vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);
    }

```

Когда есть вершины маркеров, copyMarkers записывает команды копирования в **командный буфер текущего кадра** перед началом render pass. Один барьер после копий разрешает чтение обоих буферов на стадии ввода вершин. Так загрузка динамических маркеров обходится без отдельного ожидания очереди каждый кадр.

Набор сцены привязывается один раз в слот 0. После выбора конвейера привязываются общие вершины и индексы. В цикле i=0,1 выполняются четыре действия:

1. dynamic_offset=i*model_stride — адрес нужной записи в общем UBO.
2. vkCmdBindDescriptorSets(...,1,1,&descriptor_sets[1+materials[i]],1,&dynamic_offset). Первая пара 1,1 означает «начиная со слота 1, один набор». Последняя единица — количество передаваемых динамических смещений. При выборе Doge индекс материала равен 3, значит берётся descriptor_sets[4].
3. Effect{i==0 && animated,time} разрешает дополнительную выборку только для первого draw и только при включённом флажке. vkCmdPushConstants записывает 8 байт с нулевого смещения во фрагментную стадию.
4. vkCmdDrawIndexed(...,index_count,1,0,0,0) рисует 384 индекса, один экземпляр, начиная с нулевых смещений. Команда вызывается дважды. Это два отдельных draw одной сетки, а не один instanced draw на два экземпляра.

После конусов прежний код переключается на конвейеры маркеров и линий. Запись команд завершается, существующий цикл lab2 отправляет кадр на исполнение и вывод.

## 9. Вершинный шейдер · почему в coneUV три числа

**shaders/cone.vert, строки 1–4**

```glsl
#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
```

**shaders/cone.vert, строки 21–34**

```glsl
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 coneUV;
void main() {
    vec4 world = object.model * vec4(inPosition, 1.0);
    gl_Position = scene.viewProjection * world;
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    // The side narrows to zero width at the tip. Interpolate u times that width.
    float width = inNormal.y < 0.0 ? 1.0 : inUV.y;
    // Center the pictures toward the initial camera, with their right edge on the right.
    float u = inNormal.y < 0.0 ? inUV.x : 0.65 - inUV.x;
    coneUV = vec3(u * width, inUV.y, width);
}

```

inUV приходит из location 3. Вершинный шейдер передаёт фрагментному worldPosition, worldNormal и coneUV. world вычисляет позицию модели в сцене; viewProjection переводит её в координаты отсечения, которые записываются в gl_Position. normalMatrix преобразует нормаль для освещения.

У основания inNormal.y отрицательна. Тогда width=1, u=inUV.x, и UV остаются плоской проекцией. На боку width=inUV.y: на ободе 1, на вершине 0. Выражение 0.65−inUV.x разворачивает изображение по горизонтали и ориентирует его относительно начальной камеры. На выход отправляется тройка (u*width, V, width). После интерполяции фрагмент восстановит горизонтальную координату делением. Так любое формально заданное U самой вершины умножается на 0: оно не стягивает рисунок каждого сектора к отдельной колонке изображения. max(...,1e-7) на следующем этапе защищает деление у самой верхушки.

## 10. Фрагментный шейдер · весь путь до цвета пикселя

**shaders/cone.frag, строки 2–9**

```glsl
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 coneUV;
layout(set=1,binding=1) uniform sampler2D albedoMap;
layout(set=1,binding=2) uniform sampler2D specularMap;
layout(set=1,binding=3) uniform sampler2D emissiveMap;
layout(push_constant) uniform Effect { int enabled; float time; } effect;
layout(location = 0) out vec4 outColor;
```

**shaders/cone.frag, строки 45–67**

```glsl
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 base, vec3 highlight, vec3 radiance) {
    float diffuse = max(dot(N, L), 0.0);
    if (diffuse <= 0.0) return vec3(0.0);
    vec3 H = safeNormalize(L + V);
    float specular = pow(max(dot(N, H), 0.0), object.specular.w);
    return (base * diffuse + highlight * specular) * radiance;
}
void main() {
    vec3 N = safeNormalize(worldNormal);
    vec3 V = scene.cameraPosition.w > 0.5
        ? safeNormalize(scene.cameraPosition.xyz - worldPosition)
        : -scene.viewDirection.xyz;
    vec2 uv = vec2(coneUV.x / max(coneUV.z, 1e-7), coneUV.y);
    vec3 base = texture(albedoMap, uv).rgb;
    if (effect.enabled != 0) {
        vec2 offset = 0.12 * vec2(sin(effect.time), cos(effect.time));
        vec3 layer = texture(albedoMap, uv * 2.0 + offset).rgb;
        base = mix(base, layer, 0.45) * (0.8 + 0.2 * cos(effect.time + vec3(0,2,4)));
    }
    base *= object.albedo.rgb;
    vec3 highlight = texture(specularMap, uv).rgb * object.specular.rgb;
    vec3 result = base * scene.ambient.rgb;
    result += blinnPhong(N, V, -scene.lightDirection.xyz, base, highlight, scene.lightColor.rgb);
```

sampler2D соответствует паре изображение–сэмплер. Имена albedoMap, specularMap и emissiveMap соответствуют binding 1,2,3 у материала. Блок Effect соответствует C++ структуре с int32_t и float.

В начале main нормаль приводится к единичной длине. Для перспективной камеры направление к наблюдателю вычисляется от положения фрагмента к камере; для ортографической используется постоянное направление. Восстановив uv, texture(albedoMap,uv).rgb читает основной цвет. Когда enabled равен нулю, шейдер сразу переходит к множителю albedo.rgb.

При enabled!=0 читается та же картинка второй раз. Координаты uv*2 меняют пространственный масштаб рисунка, offset=0.12*(sin(t),cos(t)) перемещает выборку по окружности. mix(base,layer,0.45) возвращает 0.55*base+0.45*layer. Следующий множитель меняется от 0.6 до 1.0, с разными фазами для R,G,B. Геометрия остаётся на месте. Specular и emissive по-прежнему читаются по исходным uv, а не по координатам второй выборки.

highlight — произведение карты блика на цвет блика материала. В blinnPhong N — нормаль, L — направление к свету, V — к камере. dot(N,L) определяет диффузную составляющую. Если свет с обратной стороны, функция возвращает ноль. H — нормализованное L+V, направление между светом и наблюдателем. Степень object.specular.w определяет узость блика. Формула (base*diffuse + highlight*specular)*radiance учитывает текстуру и освещённость источника. Ключевое изменение lab3 — в эту функцию явно передаётся highlight, зависящий от места на картинке.

**shaders/cone.frag, строки 68–93**

```glsl
    for (uint i = 0; i < scene.lightCounts.x; ++i) {
        PointLight light = points.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        // Защита от деления на ноль непосредственно в позиции источника.
        float distanceSquared = max(dot(delta, delta), 0.0001);
        vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w / distanceSquared;
        result += blinnPhong(N, V, safeNormalize(delta), base, highlight, radiance);
    }
    for (uint i = 0; i < scene.lightCounts.y; ++i) {
        SpotLight light = spots.lights[i];
        vec3 delta = light.position.xyz - worldPosition;
        vec3 L = safeNormalize(delta);
        float theta = dot(-L, light.direction.xyz);
        float innerCos = light.cone.x;
        float outerCos = light.cone.y;
        float coneFactor;
        if (innerCos - outerCos > 0.000001)
            coneFactor = clamp((theta - outerCos) / (innerCos - outerCos), 0.0, 1.0);
        else
            coneFactor = step(outerCos, theta);
        float distanceSquared = max(dot(delta, delta), 0.0001);
        vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * coneFactor / distanceSquared;
        result += blinnPhong(N, V, L, base, highlight, radiance);
    }
    outColor = vec4(result + texture(emissiveMap, uv).rgb, 1.0);
}
```

result начинается с фонового освещения и вклада направленного света. В обоих циклах источников теперь тоже передаётся highlight. Для точечного источника энергия уменьшается с квадратом расстояния. Для прожектора добавляется coneFactor: максимум во внутреннем конусе, плавное падение к внешнему краю; при совпадении углов используется step, чтобы избежать деления на ноль. Расчёт этих источников существовал в lab2; теперь их блики зависят от маски specular.

Самая последняя операция — result + texture(emissiveMap,uv).rgb. Этот член не умножается на освещённость. Поэтому остаётся виден, даже когда все источники выключены. Значение alpha равно 1. Чёрный пиксель даёт нулевую зеркальную составляющую при умножении и нулевую добавку при сложении. Emissive влияет на цвет самого фрагмента; приложение не моделирует освещение соседних объектов излучением.

## 11. Завершение и ошибки

**source/graphics.cpp, строки 518–533**

```cpp
        descriptor_sets[i] = VK_NULL_HANDLE;
    }
    for (Buffer* b : {&vertex_buffer, &index_buffer, &scene_buffer, &model_buffer,
                      &point_buffer, &spot_buffer, &marker_buffer, &line_buffer, &marker_staging, &line_staging}) destroyBuffer(*b);
    for (Texture& tex : textures) {
        vkDestroyImageView(context.device, tex.view, nullptr);
        if (tex.image) vmaDestroyImage(context.allocator, tex.image, tex.allocation);
        tex = {};
    }
    vkDestroySampler(context.device, sampler, nullptr);
    vkDestroyCommandPool(context.device, upload_pool, nullptr);
    sampler = VK_NULL_HANDLE;
    upload_pool = VK_NULL_HANDLE;
    pipeline_layout = VK_NULL_HANDLE;
    descriptor_pool = VK_NULL_HANDLE;
    index_count = 0;
```

shutdownCone сначала ожидает завершения работы устройства и освобождает конвейеры, пул и схемы дескрипторов. К списку уничтожаемых буферов добавились два staging-буфера маркеров. Для каждой текстуры уничтожается view, затем VMA освобождает изображение с его allocation. Пустые ресурсы допускаются: это важно для отсутствующей необязательной карты или прерванной инициализации. В конце уничтожаются общий sampler и upload_pool. Обнуление handle и структур позволяет проходить очистку после частично завершённой загрузки.

## 12. Сборка, ресурсы и настройки редактора

- В CMakeLists.txt имя проекта и цели изменено на lab3. Внешний stb_image.h ищется в third_party, помеченной как системный include. Реализация включена из одного .cpp; добавлять отдельную скомпилированную библиотеку для PNG не требуется.
- Четыре шейдера собираются glslc в SPIR-V для Vulkan 1.1; цель lab3 зависит от цели shaders. Файлы cone.vert и cone.frag изменились, а шейдеры маркеров используют прежнюю схему. Цикл configure_file(... COPYONLY) копирует семь PNG в build/textures. Если исходная необязательная карта отсутствует, CMake копирует black.png под нужным именем. Есть и вторая защита на этапе загрузки, если необязательный файл пропал уже рядом с executable.
- run.cmd проверяет и запускает build/lab3.exe. build.cmd использует общий комплект инструментов из lab1. В CMakePresets.json позже включён CMAKE_EXPORT_COMPILE_COMMANDS. Файл .clangd указывает каталог build с командами компиляции и цель x86_64-w64-windows-gnu. Эти настройки исправляют подсветку C++ в редакторе; отрисовка их не читает.
- Из прежнего проекта используются создание окна и устройства, swapchain, обработка изменения размера, камера, основные источники света, анимация геометрии и подготовка ImGui. Основной цикл main.cpp сохранился, поменялся заголовок окна. Код стороннего декодера отделён от собственного изменения — его контракт разобран в разделе 5.

## 13. Термины, которые легко перепутать

| Термин | Что означает здесь |
|---|---|
| Тексель / фрагмент | Тексель принадлежит исходной картинке. Фрагмент — кандидат на запись в кадр после растеризации треугольника. Один фрагмент может читать несколько текселей. |
| CPU / GPU | CPU готовит данные и записывает команды. GPU исполняет их, читает вершины и выполняет шейдеры. |
| UV / нормаль | UV отвечают на вопрос «откуда брать цвет?». Нормаль отвечает на вопрос «как поверхность ориентирована относительно света?». |
| Буфер / изображение | Буфер хранит последовательность байтов. Изображение имеет формат, размерность, слои и режим использования. Для него предусмотрена специализированная фильтрация. |
| Staging / flush / barrier / fence | Staging — место для передачи данных. Flush делает CPU-запись видимой устройству. Barrier задаёт порядок и доступ между GPU-операциями. Fence позволяет CPU дождаться завершения отправленной работы. |
| Layout изображения / layout набора | Первый задаёт режим использования изображения. Второй описывает типы, номера и стадии доступа к дескрипторам. |
| Пул команд / пул дескрипторов | Первый выделяет командные буферы. Второй выделяет наборы дескрипторов. |
| UBO / storage buffer | Из UBO шейдер читает параметры сцены или модели. Storage buffers здесь хранят массивы источников света переменной длины. |
| Сэмплер / сэмплирование | Сэмплер задаёт фильтр и режим адресации. Сэмплирование — само получение значения по координатам. |
| Push-константы / динамическое смещение | Первые передают маленький флаг и время. Второе выбирает участок общего UBO с данными модели. |

## 14. Отдельный сценарий защиты · от экрана к коду

1. **Показать управление.** Выключить Animate first texture. Выбрать Doge слева и Shark справа. Сказать: «Это одна сетка, два положения и два материала. Картинки уже загружены, выбор списка меняет набор дескрипторов».
2. **Показать эксперимент с одной переменной.** Поставить обоим Doge. Включить Animate first texture. «Обе модели используют один файл; отличается только флаг. На первом объекте добавилась выборка по движущимся координатам».
3. **Показать свечение.** Первый материал Spiral, второй Doge, эффект выключен. Отключить маркеры, удалить точечные источники и прожекторы, обнулить ambient и directional. «Видимые части Spiral даёт emissive после освещения». Вернуть направленный свет и менять направление, чтобы показать зависимый от света блик.
4. **Перейти к CPU-коду.** Открыть makeConeMesh → loadTexture → uploadMesh. Показать по одному месту: UV основания, команду копирования пикселей и барьер после записи. Объяснить, что эти действия выполняются при подготовке ресурсов.
5. **Открыть конец renderScene.** Показать 1+materials[i], i*model_stride и Effect. «Первое выбирает картинку, второе — модель, третье — режим выборки. После этого выполняется draw». Это лучшее место для ответа на вопрос, зачем два объекта.
6. **Закончить во фрагментном шейдере.** Показать texture, mix, highlight и последнюю строку. «Здесь фотография превращается в цвет поверхности с учётом света и свечения».

**Проверка понимания.** Что произойдёт, если оставить один набор материала для обоих draw? Оба покажут один материал, но сохранят разные положения. Что если обоим передать динамическое смещение 0? Оба прочитают первую матрицу и окажутся в одном месте. Что если обоим передать enabled=0? Останутся обычное текстурирование, освещение и свечение; перестанет работать только дополнительная анимированная выборка.
